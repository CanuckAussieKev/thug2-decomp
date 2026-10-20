// NOT ORIGINAL - the level editor's trigger volumes, kill zones, teleports and gaps (ours). See level_editor_triggers.h.

#include "game/front/level_editor_triggers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <sstream>

#include "game/core/crc.h"
#include "game/front/level_editor_hud.h"
#include "game/front/level_editor_layout.h"
#include "game/front/level_editor_node_edit.h"
#include "game/front/level_editor_nodes.h"
#include "game/front/level_editor_objects.h"
#include "game/front/level_editor_picking.h"
#include "game/front/level_editor_scenery.h"
#include "game/front/level_editor_view.h"
#include "game/front/level_editor_walls.h"
#include "game/mth/matrix.h"
#include "game/nx/coll_cache.h"
#include "game/nx/coll_movable.h"
#include "game/nx/coll_tri.h"
#include "game/nx/coll_world.h"
#include "game/script/load.h"
#include "game/script/node_manager.h"
#include "game/script/nodes.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "game/skate/gap_checklist.h"
#include "hook/game.h"
#include "hook/log.h"

namespace Front::LevelEditor::Triggers {

// --- the pure parts ---------------------------------------------------------------------------------------------

namespace {

bool SameText(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z')
            x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z')
            y = static_cast<char>(y - 'A' + 'a');
        if (x != y)
            return false;
    }
    return true;
}

uint32_t Crc(const char* text) { return Crc::GenerateCRCFromString(text); }

// The names the scripts use.
uint32_t kStartGap() { static const uint32_t c = Crc("StartGap"); return c; }
uint32_t kEndGap() { static const uint32_t c = Crc("EndGap"); return c; }
uint32_t kGapId() { static const uint32_t c = Crc("GapID"); return c; }
uint32_t kFlags() { static const uint32_t c = Crc("flags"); return c; }
uint32_t kScore() { static const uint32_t c = Crc("score"); return c; }
uint32_t kText() { static const uint32_t c = Crc("text"); return c; }
uint32_t kNodeName() { static const uint32_t c = Crc("nodename"); return c; }
uint32_t kKillSkater() { static const uint32_t c = Crc("SK3_KillSkater"); return c; }
uint32_t kKillSkaterWater() { static const uint32_t c = Crc("SK3_KillSkater_Water"); return c; }
uint32_t kTeleport() { static const uint32_t c = Crc("TeleportSkaterToNode"); return c; }

void Put4(std::vector<uint8_t>* out, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        out->push_back(static_cast<uint8_t>(value >> (8 * i)));
}
void PutName(std::vector<uint8_t>* out, uint32_t checksum)
{
    out->push_back(0x16);
    Put4(out, checksum);
}
// name=value (a name)
void PutNameValue(std::vector<uint8_t>* out, uint32_t key, uint32_t value)
{
    PutName(out, key);
    out->push_back(0x07);
    PutName(out, value);
}

// "0x" and 8 hex digits.
bool IsHexForm(const std::string& name, uint32_t* value)
{
    if (name.size() != 10 || name[0] != '0' || (name[1] != 'x' && name[1] != 'X'))
        return false;
    uint32_t v = 0;
    for (size_t i = 2; i < 10; ++i) {
        char c = name[i];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0)
            return false;
        v = v << 4 | static_cast<uint32_t>(d);
    }
    *value = v;
    return true;
}

bool ValidName(const std::string& name) { return NodeCreate::ValidName(name); }

bool ReadNumber(const std::string& text, float* out)
{
    char* end = nullptr;
    *out = std::strtof(text.c_str(), &end);
    return !text.empty() && end && *end == '\0' && std::isfinite(*out);
}

bool ReadTriple(const std::string& text, V3* out)
{
    float v[3];
    size_t at = 0;
    for (int i = 0; i < 3; ++i) {
        size_t comma = i < 2 ? text.find(',', at) : text.size();
        if (comma == std::string::npos)
            return false;
        if (!ReadNumber(text.substr(at, comma - at), &v[i]))
            return false;
        at = comma + 1;
    }
    *out = {v[0], v[1], v[2]};
    return true;
}

std::string Triple(V3 v)
{
    char text[96];
    std::snprintf(text, sizeof(text), "%.9g,%.9g,%.9g", v.x, v.y, v.z);
    return text;
}

} // namespace

const char* KindName(Kind kind)
{
    switch (kind) {
    case Kind::Kill: return "Kill zone";
    case Kind::Teleport: return "Teleport";
    case Kind::Script: return "Runs a script";
    case Kind::GapStart: return "Gap start";
    case Kind::GapEnd: return "Gap end";
    case Kind::Other: return "Trigger (no script)";
    }
    return "?";
}

const char* KindWord(Kind kind)
{
    switch (kind) {
    case Kind::Kill: return "kill";
    case Kind::Teleport: return "teleport";
    case Kind::Script: return "script";
    case Kind::GapStart: return "gapstart";
    case Kind::GapEnd: return "gapend";
    case Kind::Other: return "other";
    }
    return "other";
}

bool KindFromWord(const std::string& word, Kind* kind)
{
    for (int i = 0; i < kKinds; ++i) {
        if (word == KindWord(static_cast<Kind>(i))) {
            *kind = static_cast<Kind>(i);
            return true;
        }
    }
    return false;
}

uint32_t KindColour(Kind kind)
{
    switch (kind) {
    case Kind::Kill: return 0xFFFF4848;
    case Kind::Teleport: return 0xFFB27CFF;
    case Kind::Script: return 0xFF3FD4E6;
    case Kind::GapStart: return 0xFF62E26A;
    case Kind::GapEnd: return 0xFFF2D24A;
    case Kind::Other: return 0xFF8C9AAA;
    }
    return 0xFF8C9AAA;
}

Kind KindOfCalls(const Calls& calls, bool hasScript)
{
    if (calls.kill)
        return Kind::Kill;
    if (calls.teleport)
        return Kind::Teleport;
    if (calls.endGap)
        return Kind::GapEnd;
    if (calls.startGap)
        return Kind::GapStart;
    return hasScript ? Kind::Script : Kind::Other;
}

const std::vector<const char*>& GapFlags()
{
    static const std::vector<const char*> flags = {
        "PURE_AIR",       "PURE_GRIND",     "PURE_RAIL",       "PURE_MANUAL",    "CANCEL_GROUND", "CANCEL_AIR",
        "CANCEL_RAIL",    "CANCEL_GRIND",   "CANCEL_MANUAL",   "CANCEL_WALL",    "CANCEL_WALLRIDE",
        "CANCEL_LIP",     "CANCEL_HANG",    "CANCEL_WALK",     "REQUIRE_AIR",    "REQUIRE_RAIL",  "REQUIRE_GRIND",
        "REQUIRE_MANUAL", "REQUIRE_LIP",    "REQUIRE_WALL",    "REQUIRE_WALLRIDE", "REQUIRE_WALK"};
    return flags;
}

bool SameSpec(const Spec& a, const Spec& b)
{
    return a.name == b.name && a.kind == b.kind && a.size.x == b.size.x && a.size.y == b.size.y &&
           a.size.z == b.size.z && a.water == b.water && a.target == b.target && a.script == b.script &&
           a.gap == b.gap && a.flags == b.flags && a.text == b.text && a.score == b.score && a.replaces == b.replaces;
}

uint32_t ChecksumOf(const std::string& name)
{
    uint32_t value = 0;
    if (IsHexForm(name, &value))
        return value;
    return name.empty() ? 0u : Crc(name.c_str());
}

std::string NameFor(uint32_t checksum, const char* known)
{
    if (known && *known && ValidName(known) && Crc(known) == checksum)
        return known;
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08X", checksum);
    return text;
}

std::string ScriptName(const Spec& spec) { return spec.name + "Script"; }

const char* Problem(const Spec& spec)
{
    switch (spec.kind) {
    case Kind::Kill:
        return spec.target.empty() ? "Choose the restart the skater goes back to" : nullptr;
    case Kind::Teleport:
        return spec.target.empty() ? "Choose where it sends the skater" : nullptr;
    case Kind::Script:
        return spec.script.empty() ? "Choose the script it runs" : nullptr;
    case Kind::GapStart:
        return spec.gap.empty() ? "It has no gap" : nullptr;
    case Kind::GapEnd:
        if (spec.gap.empty())
            return "It has no gap";
        if (spec.text.empty() || spec.score <= 0)
            return "A gap needs its name and a score (the gap list takes only those)";
        return nullptr;
    case Kind::Other:
        return "It has no script";
    }
    return "?";
}

std::string CleanText(const std::string& text)
{
    std::string out;
    for (char c : text) {
        unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F)
            continue;
        if (c == '"')
            c = '\'';
        if (c == '#')
            continue;
        out.push_back(c);
        if (out.size() >= 60)
            break;
    }
    // (no spaces at either end)
    size_t first = out.find_first_not_of(' ');
    if (first == std::string::npos)
        return "";
    size_t last = out.find_last_not_of(' ');
    return out.substr(first, last - first + 1);
}

V3 ClampSize(V3 size)
{
    auto clamp = [](float v) { return std::isfinite(v) ? std::max(10.0f, std::min(50000.0f, v)) : 10.0f; };
    return {clamp(size.x), clamp(size.y), clamp(size.z)};
}

OrientedBox BoxOf(V3 pos, float yaw, V3 size)
{
    OrientedBox box;
    box.origin = pos;
    box.axes[0] = TurnAboutUp({1.0f, 0.0f, 0.0f}, yaw);
    box.axes[1] = {0.0f, 1.0f, 0.0f};
    box.axes[2] = TurnAboutUp({0.0f, 0.0f, 1.0f}, yaw);
    box.min = size * -0.5f;
    box.max = size * 0.5f;
    return box;
}

std::vector<uint8_t> Body(const Spec& spec)
{
    std::vector<uint8_t> out;
    if (Problem(spec))
        return out;
    switch (spec.kind) {
    case Kind::Kill:
        PutName(&out, spec.water ? kKillSkaterWater() : kKillSkater());
        PutNameValue(&out, kNodeName(), ChecksumOf(spec.target));
        out.push_back(0x01);
        break;
    case Kind::Teleport:
        PutName(&out, kTeleport());
        PutNameValue(&out, kNodeName(), ChecksumOf(spec.target));
        out.push_back(0x01);
        break;
    case Kind::Script:
        PutName(&out, ChecksumOf(spec.script));
        out.push_back(0x01);
        break;
    case Kind::GapStart:
        PutName(&out, kStartGap());
        PutNameValue(&out, kGapId(), ChecksumOf(spec.gap));
        if (!spec.flags.empty()) {
            PutName(&out, kFlags());
            out.push_back(0x07);
            out.push_back(0x05);
            for (size_t i = 0; i < spec.flags.size(); ++i) {
                if (i)
                    out.push_back(0x09);
                PutName(&out, ChecksumOf(spec.flags[i]));
            }
            out.push_back(0x06);
        }
        out.push_back(0x01);
        break;
    case Kind::GapEnd: {
        PutName(&out, kEndGap());
        PutNameValue(&out, kGapId(), ChecksumOf(spec.gap));
        PutName(&out, kScore());
        out.push_back(0x07);
        out.push_back(0x17);
        Put4(&out, static_cast<uint32_t>(spec.score));
        PutName(&out, kText());
        out.push_back(0x07);
        out.push_back(0x1B);
        std::string text = CleanText(spec.text);
        Put4(&out, static_cast<uint32_t>(text.size() + 1));
        out.insert(out.end(), text.begin(), text.end());
        out.push_back(0x00);
        out.push_back(0x01);
        break;
    }
    case Kind::Other:
        break;
    }
    return out;
}

std::vector<uint8_t> QbFile(const std::vector<Spec>& specs)
{
    std::vector<uint8_t> out;
    std::vector<std::pair<uint32_t, std::string>> names;
    auto note = [&](const std::string& name) {
        uint32_t value;
        if (name.empty() || IsHexForm(name, &value))
            return;
        uint32_t checksum = Crc(name.c_str());
        for (const auto& n : names) {
            if (n.first == checksum)
                return;
        }
        names.push_back({checksum, name});
    };
    for (const Spec& spec : specs) {
        out.push_back(0x23);
        std::string script = ScriptName(spec);
        PutName(&out, Crc(script.c_str()));
        out.push_back(0x01);
        std::vector<uint8_t> body = Body(spec);
        out.insert(out.end(), body.begin(), body.end());
        out.push_back(0x24);
        out.push_back(0x01);
        note(script);
        if (!body.empty()) {
            if (spec.kind == Kind::Kill || spec.kind == Kind::Teleport)
                note(spec.target);
            if (spec.kind == Kind::Script)
                note(spec.script);
            if (spec.kind == Kind::GapStart || spec.kind == Kind::GapEnd)
                note(spec.gap);
        }
    }
    for (const auto& n : names) {
        out.push_back(0x2B);
        Put4(&out, n.first);
        out.insert(out.end(), n.second.begin(), n.second.end());
        out.push_back(0x00);
    }
    out.push_back(0x00);
    return out;
}

std::string Record(const Placed& placed)
{
    const Spec& s = placed.spec;
    std::ostringstream out;
    out << s.name << ' ' << KindWord(s.kind) << " pos=" << Triple(placed.pos);
    char number[48];
    std::snprintf(number, sizeof(number), " yaw=%.9g", placed.yaw);
    out << number << " size=" << Triple(s.size);
    if (!s.target.empty())
        out << " target=" << s.target;
    if (s.water)
        out << " water=1";
    if (!s.script.empty())
        out << " script=" << s.script;
    if (!s.gap.empty())
        out << " gap=" << s.gap;
    if (!s.flags.empty()) {
        out << " flags=";
        for (size_t i = 0; i < s.flags.size(); ++i)
            out << (i ? "," : "") << s.flags[i];
    }
    if (s.kind == Kind::GapStart || s.kind == Kind::GapEnd)
        out << " score=" << s.score;
    if (s.replaces) {
        std::snprintf(number, sizeof(number), " replaces=%08X", s.replaces);
        out << number;
    }
    std::string text = CleanText(s.text);
    if (!text.empty())
        out << " text=\"" << text << '"';
    return out.str();
}

bool ReadRecord(const std::string& line, Placed* placed)
{
    *placed = Placed{};
    Spec& s = placed->spec;
    std::string text = line;
    // text="..." (last: it may hold spaces)
    size_t quote = text.find(" text=\"");
    if (quote != std::string::npos) {
        size_t start = quote + 7;
        size_t end = text.find('"', start);
        if (end == std::string::npos || text.find_first_not_of(" \t\r", end + 1) != std::string::npos)
            return false;
        s.text = CleanText(text.substr(start, end - start));
        text.resize(quote);
    }
    std::istringstream in(text);
    std::string word;
    if (!(in >> s.name >> word) || !ValidName(s.name) || !KindFromWord(word, &s.kind) || s.kind == Kind::Other)
        return false;
    bool havePos = false, haveYaw = false, haveSize = false, haveTarget = false, haveWater = false, haveScript = false,
         haveGap = false, haveFlags = false, haveScore = false, haveReplaces = false;
    s.score = 100;
    auto once = [](bool* seen) {
        if (*seen)
            return false;
        *seen = true;
        return true;
    };
    std::string field;
    while (in >> field) {
        size_t equals = field.find('=');
        if (equals == std::string::npos || equals == 0)
            return false;
        std::string key = field.substr(0, equals), value = field.substr(equals + 1);
        bool ok = true;
        if (key == "pos") {
            ok = once(&havePos) && ReadTriple(value, &placed->pos);
        } else if (key == "yaw") {
            ok = once(&haveYaw) && ReadNumber(value, &placed->yaw);
        } else if (key == "size") {
            ok = once(&haveSize) && ReadTriple(value, &s.size);
            s.size = ClampSize(s.size);
        } else if (key == "target") {
            ok = once(&haveTarget) && ValidName(value);
            s.target = value;
        } else if (key == "water") {
            ok = once(&haveWater) && (value == "0" || value == "1");
            s.water = value == "1";
        } else if (key == "script") {
            ok = once(&haveScript) && ValidName(value);
            s.script = value;
        } else if (key == "gap") {
            ok = once(&haveGap) && ValidName(value);
            s.gap = value;
        } else if (key == "flags") {
            ok = once(&haveFlags);
            size_t at = 0;
            while (ok && at <= value.size()) {
                size_t comma = value.find(',', at);
                if (comma == std::string::npos)
                    comma = value.size();
                std::string flag = value.substr(at, comma - at);
                ok = ValidName(flag);
                s.flags.push_back(flag);
                at = comma + 1;
            }
        } else if (key == "score") {
            char* end = nullptr;
            long score = std::strtol(value.c_str(), &end, 10);
            ok = once(&haveScore) && !value.empty() && end && *end == '\0' && score >= 0 && score <= 1000000;
            s.score = static_cast<int>(score);
        } else if (key == "replaces") {
            char* end = nullptr;
            unsigned long checksum = std::strtoul(value.c_str(), &end, 16);
            ok = once(&haveReplaces) && !value.empty() && value.size() <= 8 && end && *end == '\0';
            s.replaces = static_cast<uint32_t>(checksum);
        } // (another key: a later version's - skipped)
        if (!ok)
            return false;
    }
    return havePos;
}

int Partner(const std::vector<Spec>& specs, int index)
{
    if (index < 0 || index >= static_cast<int>(specs.size()))
        return -1;
    const Spec& s = specs[index];
    Kind other;
    if (s.kind == Kind::GapStart)
        other = Kind::GapEnd;
    else if (s.kind == Kind::GapEnd)
        other = Kind::GapStart;
    else
        return -1;
    for (size_t i = 0; i < specs.size(); ++i) {
        if (static_cast<int>(i) != index && specs[i].kind == other && !s.gap.empty() && SameText(specs[i].gap, s.gap))
            return static_cast<int>(i);
    }
    return -1;
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kCollStaticVtable = 0x00645C18;
constexpr uint32_t kTriggerScript = 0x2CA8A299; // TriggerScript
constexpr uint16_t kVolumeFlags = 0x50;         // a trigger face the skater passes through
constexpr char kFileName[] = "editor\\thug2_decomp_triggers.qb";
constexpr float kDrawRadius = 8000.0f;   // (level triggers drawn within this of the camera)
constexpr float kLabelRadius = 3000.0f;
constexpr int kMaxLevelDrawn = 300, kMaxLabels = 48, kMaxFacesKept = 64;

// The editor's triggers (alive or not: a deleted one's spec is kept for its undo), what Tick saw of them.
std::vector<Spec> g_specs;
std::vector<Added> g_added;
// The level's triggers (scanned when the editor starts in a level); those switched off / on here (their walls' states
// go in the undo steps); those switched off while an editable copy stands in for them (on again when it goes).
std::vector<LevelTrigger> g_level;
bool g_scanned;
std::vector<uint32_t> g_switched;
std::vector<uint32_t> g_turnedOff;
uint32_t g_selectWall;
uint32_t g_levelArray;
uint32_t g_inspected;
// The collision boxes.
struct Box {
    uint32_t name;
    Nx::CCollMovBBox* coll;
    V3 pos, size;
    float yaw;
};
std::vector<Box> g_boxes;
// The scripts last defined, the gaps the editor put in the checklist (their names and scores) and the checklist.
std::vector<uint8_t> g_defined;
std::vector<std::pair<std::string, int>> g_gapsAdded;
Game::CGapChecklist* g_checklist;
std::vector<std::string> g_levelGapNames;
bool g_levelGapsRead;
// Undo, selection, the UI's actions.
bool g_hasUndo;
State g_undoBefore;
int g_select = -1;
std::vector<std::function<void()>> g_actions;
bool g_shown, g_settingRead;
V3 g_camera = {}, g_forward = {0.0f, 0.0f, 1.0f};
float g_yaw;

uint32_t NameChecksum(const std::string& name) { return NodeCreate::Checksum(name); }

std::string LabelOf(uint32_t checksum) { return Nodes::Label(checksum); }

// The overlay's trigger nodes now.
bool NodeAlive(const std::string& name) { return NodeCreate::IsAdded(NameChecksum(name)); }

bool NodePlace(const std::string& name, V3* pos, float* yaw)
{
    Script::CStruct* node = SkateScript::NodeManagerInstance() ? SkateScript::GetNode(NameChecksum(name), false) : nullptr;
    if (!node)
        return false;
    Mth::Vector p = {0.0f, 0.0f, 0.0f, 1.0f}, a = {0.0f, 0.0f, 0.0f, 0.0f};
    SkateScript::GetPosition(node, &p);
    SkateScript::GetAngles(node, &a);
    *pos = {p.x, p.y, p.z};
    *yaw = a.y;
    return true;
}

// --- what a script calls ---

Calls* g_calls;
void __cdecl OnKill(Script::CStruct* params, const uint8_t*)
{
    g_calls->kill = true;
    uint32_t target = 0;
    if (params && params->GetChecksum(kNodeName(), &target, 0) && target)
        g_calls->target = target;
}
void __cdecl OnWater(Script::CStruct* params, const uint8_t* call)
{
    OnKill(params, call);
    g_calls->water = true;
}
void __cdecl OnTeleport(Script::CStruct* params, const uint8_t*)
{
    g_calls->teleport = true;
    uint32_t target = 0;
    if (params && params->GetChecksum(kNodeName(), &target, 0) && target)
        g_calls->target = target;
}
void __cdecl OnStartGap(Script::CStruct* params, const uint8_t*)
{
    g_calls->startGap = true;
    uint32_t id = 0;
    if (params && params->GetChecksum(kGapId(), &id, 0) && !g_calls->gapId)
        g_calls->gapId = id;
}
void __cdecl OnEndGap(Script::CStruct* params, const uint8_t*)
{
    g_calls->endGap = true;
    uint32_t id = 0;
    if (params && params->GetChecksum(kGapId(), &id, 0))
        g_calls->gapId = id;
    const char* text = nullptr;
    if (params && params->GetString(kText(), &text, 0) && text)
        g_calls->gapText = text;
    int score = 0;
    if (params && params->GetInteger(kScore(), &score, 0))
        g_calls->gapScore = score;
}

Calls Walk(uint32_t script);

// What a script calls, walked once a level (a trigger that runs a script: shown every frame).
std::vector<std::pair<uint32_t, Calls>> g_callsCache;
const Calls& CallsOf(uint32_t script)
{
    for (const auto& c : g_callsCache) {
        if (c.first == script)
            return c.second;
    }
    g_callsCache.push_back({script, Walk(script)});
    return g_callsCache.back().second;
}

bool IsScript(uint32_t name)
{
    Script::CSymbolTableEntry* symbol = name ? Script::LookUpSymbol(name) : nullptr;
    return symbol && symbol->type == Script::ESYMBOLTYPE_QSCRIPT;
}

Calls Walk(uint32_t script)
{
    Calls calls;
    if (!IsScript(script))
        return calls;
    g_calls = &calls;
    static const uint32_t kills[] = {kKillSkater(), Crc("SK3_KillSkater_Pungee"), Crc("SK3_Killskater_Finish"),
                                     Crc("KillSkater")};
    Script::ClearScriptFlag6F2564(); // (as ForEachNodeScriptCall walks them)
    for (uint32_t target : kills)
        SkateScript::ForEachScriptCall(script, target, &OnKill, nullptr, 0);
    SkateScript::ForEachScriptCall(script, kKillSkaterWater(), &OnWater, nullptr, 0);
    SkateScript::ForEachScriptCall(script, kTeleport(), &OnTeleport, nullptr, 0);
    SkateScript::ForEachScriptCall(script, kStartGap(), &OnStartGap, nullptr, 0);
    SkateScript::ForEachScriptCall(script, kEndGap(), &OnEndGap, nullptr, 0);
    Script::SetScriptFlag6F2564();
    g_calls = nullptr;
    return calls;
}

// --- the level's triggers ---

uint8_t* CollOf(uint8_t* sector)
{
    uint8_t* coll = sector ? *reinterpret_cast<uint8_t**>(sector + 0x18) : nullptr;
    return coll && *reinterpret_cast<uint32_t*>(coll) == kCollStaticVtable ? coll : nullptr;
}
bool TurnedOffByUs(uint32_t sector)
{
    return std::find(g_turnedOff.begin(), g_turnedOff.end(), sector) != g_turnedOff.end();
}

V3 ToV3(const Mth::Vector& v) { return {v.x, v.y, v.z}; }

// A sector's trigger faces read into `t` (false: none) - its faces' box, the first ones' corners, whether every face is
// a trigger's; where its collision is now (a wall moved: read again - RefreshMoved).
bool ReadFaces(uint8_t* sector, LevelTrigger* t)
{
    uint8_t* coll = CollOf(sector);
    auto* tri = coll ? *reinterpret_cast<Nx::CCollObjTriData**>(coll + 0x0C) : nullptr;
    if (!tri || !tri->m_faces || !tri->m_vertices || tri->m_numFaces == 0)
        return false;
    t->faces.clear();
    t->faceCount = 0;
    t->allTrigger = true;
    t->wall = *reinterpret_cast<uint8_t**>(sector + 0x14) == nullptr; // (no geometry: collision only)
    t->boxMin = {tri->m_min.x, tri->m_min.y, tri->m_min.z};
    t->boxMax = {tri->m_max.x, tri->m_max.y, tri->m_max.z};
    bool any = false;
    for (int f = 0; f < tri->m_numFaces; ++f) {
        uint16_t flags = *reinterpret_cast<const uint16_t*>(tri->m_faces + f * (tri->m_smallFaces ? 8 : 10));
        if (!(flags & 0x40)) {
            t->allTrigger = false;
            continue;
        }
        V3 p[3];
        bool ok = true;
        for (int k = 0; k < 3 && ok; ++k) {
            uint32_t index = tri->FaceVertex(f, k);
            ok = index < tri->m_numVertices;
            if (ok)
                p[k] = ToV3(tri->GetRawVertexPos(static_cast<int>(index)));
        }
        if (!ok)
            continue;
        for (const V3& q : p) {
            GrowBox(&t->min, &t->max, any, q, q);
            any = true;
        }
        if (t->faceCount < kMaxFacesKept)
            t->faces.insert(t->faces.end(), p, p + 3);
        ++t->faceCount;
    }
    return any;
}

void ScanSector(void*, uint32_t checksum, uint8_t* sector)
{
    LevelTrigger t;
    if (!ReadFaces(sector, &t))
        return;
    t.sector = checksum;
    if (Script::CStruct* node = SkateScript::GetNode(checksum, false)) {
        t.hasNode = true;
        node->GetChecksum(kTriggerScript, &t.script, 0);
    }
    g_level.push_back(std::move(t));
}

void ScanLevel()
{
    g_level.clear();
    g_scanned = true;
    if (!SkateScript::NodeManagerInstance() || !Scenery::MainScene())
        return;
    Scenery::ForEachLevelSector(&ScanSector, nullptr);
    for (LevelTrigger& t : g_level) {
        t.calls = Walk(t.script);
        t.kind = KindOfCalls(t.calls, t.script != 0);
    }
    Log("level editor: %u trigger volumes in the level", static_cast<unsigned>(g_level.size()));
}

// The level's own gaps' names (every EndGap its nodes' TriggerScripts reach - not the editor's): the gaps the editor
// never takes out of the checklist.
std::vector<std::string>* g_names;
void __cdecl OnLevelGap(Script::CStruct* params, const uint8_t*)
{
    const char* text = nullptr;
    if (params && params->GetString(kText(), &text, 0) && text)
        g_names->push_back(text);
}
void ReadLevelGaps()
{
    g_levelGapNames.clear();
    g_levelGapsRead = true;
    std::vector<uint32_t> scripts;
    for (const Nodes::Node& node : Nodes::All()) {
        if (node.added || !node.triggerScript)
            continue;
        Script::CStruct* s = Nodes::StructOf(node);
        uint32_t script = 0;
        if (s && s->GetChecksum(kTriggerScript, &script, 0) && script &&
            std::find(scripts.begin(), scripts.end(), script) == scripts.end())
            scripts.push_back(script);
    }
    g_names = &g_levelGapNames;
    Script::ClearScriptFlag6F2564();
    for (uint32_t script : scripts) {
        if (IsScript(script))
            SkateScript::ForEachScriptCall(script, kEndGap(), &OnLevelGap, nullptr, 0);
    }
    Script::SetScriptFlag6F2564();
    g_names = nullptr;
}

// --- the boxes ---

Nx::CCollMovBBox* MakeBox(uint32_t name)
{
    void* memory = ::operator new(sizeof(Nx::CCollMovBBox), std::nothrow);
    if (!memory)
        return nullptr;
    Nx::CCollMovBBox* box = static_cast<Nx::CCollMovBBox*>(memory)->Construct();
    box->m_checksum = name;
    box->m_faceFlags = kVolumeFlags;
    box->m_terrain = 0;
    Nx::RegisterMovable(box);
    return box;
}

void FreeBox(Nx::CCollMovBBox* box)
{
    Nx::UnregisterMovable(box);
    box->Destruct(); // (out of the collision caches too)
    ::operator delete(box);
}

void PlaceBox(Box* b, V3 pos, float yaw, V3 size)
{
    OrientedBox placed = BoxOf(pos, yaw, size);
    b->coll->m_box.min = {placed.min.x, placed.min.y, placed.min.z, 1.0f};
    b->coll->m_box.max = {placed.max.x, placed.max.y, placed.max.z, 1.0f};
    b->coll->m_position = {pos.x, pos.y, pos.z, 1.0f};
    Mth::Matrix m;
    for (int r = 0; r < 3; ++r)
        m.row[r] = {placed.axes[r].x, placed.axes[r].y, placed.axes[r].z, 0.0f};
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    b->coll->SetOrientation(&m);
    b->pos = pos;
    b->yaw = yaw;
    b->size = size;
}

void DropBoxes()
{
    for (Box& b : g_boxes)
        FreeBox(b.coll);
    g_boxes.clear();
}

// --- the gap checklist ---

Game::CGapChecklist* Checklist()
{
    void* skate = GameVariable<void*>(0x007CE478);
    return skate ? GameFunction<Game::CGapChecklist*(__thiscall*)(void*)>(0x005656C0)(skate) : nullptr;
}

bool LevelGap(const std::string& name)
{
    return std::find(g_levelGapNames.begin(), g_levelGapNames.end(), name) != g_levelGapNames.end();
}

// The editor's gaps that are no longer wanted out of `checklist` (a level's own gap of that name stays).
void TakeOutGaps(Game::CGapChecklist* checklist, const std::vector<std::pair<std::string, int>>& wanted)
{
    if (!checklist)
        return;
    for (const auto& gap : g_gapsAdded) {
        if (std::find(wanted.begin(), wanted.end(), gap) != wanted.end() || LevelGap(gap.first))
            continue;
        int index = checklist->FindGap(gap.first.c_str());
        Game::CGapCheck* check = index >= 0 ? checklist->GetGap(index) : nullptr;
        if (check && check->m_score == gap.second) {
            check->Destroy(1);
            Log("level editor: gap \"%s\" taken out of the gap list", gap.first.c_str());
        }
    }
}

// --- syncing what the specs say with the game ---

const char* GameProblem(const Spec& spec)
{
    if (const char* problem = Problem(spec))
        return problem;
    if ((spec.kind == Kind::Kill || spec.kind == Kind::Teleport) &&
        !SkateScript::GetNode(ChecksumOf(spec.target), false))
        return "That node isn't in this level";
    if (spec.kind == Kind::Script && !IsScript(ChecksumOf(spec.script)))
        return "There is no script of that name";
    return nullptr;
}

// The level's triggers as they are now: switched off or on (Walls::Off - the editor's switch, or a level script's Kill),
// and moved (an invisible wall moved by level_editor_walls.h: its faces read again when its collision's box changed).
void RefreshLevel()
{
    for (LevelTrigger& t : g_level) {
        uint8_t* sector = Scenery::SectorOf(t.sector);
        uint8_t* coll = sector ? CollOf(sector) : nullptr;
        auto* tri = coll ? *reinterpret_cast<Nx::CCollObjTriData**>(coll + 0x0C) : nullptr;
        if (!tri)
            continue;
        t.off = Walls::Off(t.sector) || (*reinterpret_cast<uint16_t*>(coll + 0x08) & 4) != 0;
        if (tri->m_min.x != t.boxMin.x || tri->m_min.y != t.boxMin.y || tri->m_min.z != t.boxMin.z ||
            tri->m_max.x != t.boxMax.x || tri->m_max.y != t.boxMax.y || tri->m_max.z != t.boxMax.z) {
            LevelTrigger now = t;
            if (ReadFaces(sector, &now)) {
                t.faces = now.faces;
                t.min = now.min;
                t.max = now.max;
            }
            t.boxMin = now.boxMin;
            t.boxMax = now.boxMax;
        }
    }
}

int SpecIndex(const std::string& name)
{
    for (size_t i = 0; i < g_specs.size(); ++i) {
        if (SameText(g_specs[i].name, name))
            return static_cast<int>(i);
    }
    return -1;
}

void Sync()
{
    // What each spec is now.
    g_added.clear();
    for (const Spec& spec : g_specs) {
        Added a;
        a.spec = spec;
        a.alive = NodeAlive(spec.name) && NodePlace(spec.name, &a.pos, &a.yaw);
        a.problem = GameProblem(spec);
        if (spec.kind == Kind::Script && !a.problem)
            a.calls = CallsOf(ChecksumOf(spec.script));
        g_added.push_back(a);
    }
    // Their scripts (every spec's: a deleted one's undo brings its node back naming it). The game's loader defines
    // them (an unchanged script is left as it is).
    std::vector<Spec> scripted;
    for (const Added& a : g_added) {
        Spec s = a.spec;
        if (a.problem)
            s.kind = Kind::Other; // (an empty script: it does nothing yet)
        scripted.push_back(s);
    }
    std::vector<uint8_t> file = scripted.empty() ? std::vector<uint8_t>() : QbFile(scripted);
    bool redefined = false;
    if (file != g_defined && !file.empty()) {
        Script::LoadQBData(kFileName, file.data(), 0, 0, 0);
        g_defined = file;
        redefined = true;
    }
    // The boxes: one for each alive trigger, where its node is (none in a network session - Guard).
    bool network = Layout::NetworkActive();
    for (size_t i = 0; i < g_boxes.size();) {
        int at = -1;
        for (size_t k = 0; k < g_added.size(); ++k) {
            if (g_added[k].alive && NameChecksum(g_added[k].spec.name) == g_boxes[i].name)
                at = static_cast<int>(k);
        }
        if (at < 0 || network) {
            FreeBox(g_boxes[i].coll);
            g_boxes.erase(g_boxes.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    for (const Added& a : g_added) {
        if (!a.alive || network)
            continue;
        uint32_t name = NameChecksum(a.spec.name);
        auto it = std::find_if(g_boxes.begin(), g_boxes.end(), [&](const Box& b) { return b.name == name; });
        if (it == g_boxes.end()) {
            Box b = {name, MakeBox(name), {}, {}, 0.0f};
            if (!b.coll)
                continue;
            PlaceBox(&b, a.pos, a.yaw, a.spec.size);
            g_boxes.push_back(b);
            Log("level editor: trigger %s (%s) in place", a.spec.name.c_str(), KindName(a.spec.kind));
        } else if (it->pos.x != a.pos.x || it->pos.y != a.pos.y || it->pos.z != a.pos.z || it->yaw != a.yaw ||
                   it->size.x != a.spec.size.x || it->size.y != a.spec.size.y || it->size.z != a.spec.size.z) {
            PlaceBox(&*it, a.pos, a.yaw, a.spec.size);
        }
    }
    // The level's triggers an alive editable copy stands in for: switched off (through level_editor_walls.h - the
    // collision switch's one owner: CSector::SetActive for a wall), on again once no copy stands in for them.
    std::vector<uint32_t> replaced;
    for (const Added& a : g_added) {
        if (a.alive && a.spec.replaces && Scenery::SectorOf(a.spec.replaces))
            replaced.push_back(a.spec.replaces);
    }
    for (size_t i = 0; i < g_turnedOff.size();) {
        if (std::find(replaced.begin(), replaced.end(), g_turnedOff[i]) == replaced.end()) {
            Walls::SetOff(g_turnedOff[i], false);
            g_turnedOff.erase(g_turnedOff.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    for (uint32_t sector : replaced) {
        if (TurnedOffByUs(sector))
            continue;
        if (!Walls::Off(sector))
            Walls::SetOff(sector, true);
        g_turnedOff.push_back(sector);
    }
    RefreshLevel();
    // The gap list: built again as the level's load builds it; the editor's gaps no longer there taken out.
    std::vector<std::pair<std::string, int>> wanted;
    for (const Added& a : g_added) {
        if (a.alive && !a.problem && a.spec.kind == Kind::GapEnd)
            wanted.push_back({CleanText(a.spec.text), a.spec.score});
    }
    std::sort(wanted.begin(), wanted.end());
    if (redefined || wanted != g_gapsAdded) {
        Game::CGapChecklist* checklist = Checklist();
        if (checklist && !Script::GetInteger(0xF7ECD937u)) {
            if (!g_levelGapsRead)
                ReadLevelGaps();
            checklist->Build();
            TakeOutGaps(checklist, wanted);
            g_checklist = checklist;
            if (wanted != g_gapsAdded)
                Log("level editor: gap list built again - %u of the editor's gaps", static_cast<unsigned>(wanted.size()));
            g_gapsAdded = wanted;
        }
    }
}

// The level's array going (NodeCreate's listener): everything of this level forgotten - its boxes unregistered, the
// editor's gaps out of its checklist (kept by the career for the level).
void Forget()
{
    DropBoxes();
    TakeOutGaps(g_checklist, {});
    g_gapsAdded.clear();
    g_checklist = nullptr;
    g_specs.clear();
    g_added.clear();
    g_switched.clear();
    g_turnedOff.clear(); // (their collision goes with the level)
    g_selectWall = 0;
    g_level.clear();
    g_scanned = false;
    g_levelGapNames.clear();
    g_levelGapsRead = false;
    g_callsCache.clear();
    g_inspected = 0;
    g_hasUndo = false;
    g_select = -1;
    g_actions.clear();
    g_levelArray = 0;
}

void OnArrayRemoved(uint32_t name)
{
    if (!name || name != g_levelArray)
        return;
    if (!g_specs.empty())
        Log("level editor: the level's node array left - its %u triggers go with it", static_cast<unsigned>(g_specs.size()));
    Forget();
}

bool ArrayLoaded(SkateScript::CNodeManager* manager, uint32_t name)
{
    for (uint32_t i = 0; i < manager->m_count && i < static_cast<uint32_t>(SkateScript::CNodeManager::kMaxArrays); ++i) {
        if (manager->m_arrays[i].m_name == name)
            return true;
    }
    return false;
}

// The level's node array noted (the triggers belong to it): another array added after it (a cutscene's) doesn't
// change it; the level's gone and another there forgets them.
void NoteLevel()
{
    NodeCreate::SetArrayRemovedListener(&OnArrayRemoved);
    if (SkateScript::CNodeManager* manager = SkateScript::NodeManagerInstance()) {
        if (g_levelArray && ArrayLoaded(manager, g_levelArray))
            return;
        uint32_t level = manager->GetLastNodeArrayName();
        if (level && level != g_levelArray) {
            if (g_levelArray)
                Forget();
            g_levelArray = level;
        }
    }
}

void NoteUndo()
{
    if (!g_hasUndo) {
        g_undoBefore = Save();
        g_hasUndo = true;
    }
}

// A node for a spec at `pos` / `yaw` in the overlay (false: `message`).
NodeCreate::Spec NodeSpec(const Spec& spec, V3 pos, float yaw)
{
    NodeCreate::Spec n;
    n.name = spec.name;
    n.cls = "GenericNode";
    n.type = "ed_trigger";
    n.pos = pos;
    n.angles = {0.0f, yaw, 0.0f};
    NodeCreate::Field script = {NodeCreate::Field::kName, "TriggerScript"};
    script.text = ScriptName(spec);
    n.extra.push_back(script);
    n.owned = true;
    return n;
}

bool NameTaken(const std::string& name) { return SpecIndex(name) >= 0 || NodeCreate::NameTaken(name); }

std::string FreshGap()
{
    return NodeCreate::FreshName("ed_gap", [](const std::string& id) {
        return std::any_of(g_specs.begin(), g_specs.end(), [&](const Spec& s) { return SameText(s.gap, id); });
    });
}

// The nearest restart (a player or multiplayer one) to `pos` ("" none).
std::string NearestRestart(V3 pos)
{
    float best = 0.0f;
    std::string name;
    for (const Nodes::Node& node : Nodes::All()) {
        if (node.family != Nodes::Family::Spawn || node.hidden || !node.hasPos ||
            !(node.restartTypes & (Nodes::kPlayer1 | Nodes::kMultiplayer)))
            continue;
        V3 d = node.pos - pos;
        float distance = Dot(d, d);
        if (name.empty() || distance < best) {
            best = distance;
            name = NameFor(node.name, Script::KeptQbName(node.name));
        }
    }
    return name;
}

// The game's collision ray (a CFeeler: constructor 0x00417760; 0x00417710 casts it from `from` to `to` - thiscall, true
// on a hit, its point at +0x34), static collision only (as the editor's G): trigger faces (0x10) are passed.
bool Cast(V3 from, V3 to, V3* hit)
{
    alignas(16) uint8_t feeler[0x100] = {};
    GameFunction<void*(__thiscall*)(void*)>(0x00417760)(feeler);
    Mth::Vector start = {from.x, from.y, from.z, 1.0f}, end = {to.x, to.y, to.z, 1.0f};
    if (!GameFunction<bool(__thiscall*)(void*, const Mth::Vector*, const Mth::Vector*, int)>(0x00417710)(
            feeler, &start, &end, 0))
        return false;
    const float* point = reinterpret_cast<const float*>(feeler + 0x34);
    *hit = {point[0], point[1], point[2]};
    return true;
}
bool GroundBelow(V3 from, V3* hit) { return Cast(from, {from.x, from.y - 5000.0f, from.z}, hit); }

// Where a new trigger goes: the ground where the view looks (else 600 units ahead), its box standing on it.
V3 PlaceAhead(V3 size, float ahead)
{
    V3 forward = Normalize(g_forward);
    V3 at;
    if (!Cast(g_camera, g_camera + forward * 4000.0f, &at))
        at = g_camera + forward * 600.0f;
    V3 flat = Normalize({forward.x, 0.0f, forward.z});
    at = at + flat * ahead;
    V3 ground;
    if (GroundBelow(at + V3{0.0f, 200.0f, 0.0f}, &ground))
        at.y = ground.y;
    at.y += size.y * 0.5f;
    return at;
}

// New triggers made (their nodes in the overlay); the first selected. One undo step.
void Create(std::vector<Placed> made, const char* what)
{
    NoteUndo();
    std::string message;
    int first = -1;
    for (Placed& p : made) {
        if (!NodeCreate::Create(NodeSpec(p.spec, p.pos, p.yaw), -1, &message)) {
            Hud::Message(message, 5.0);
            break;
        }
        g_specs.push_back(p.spec);
        if (first < 0)
            first = static_cast<int>(g_specs.size()) - 1;
        Log("level editor: added trigger %s (%s) at (%.1f, %.1f, %.1f)", p.spec.name.c_str(), KindName(p.spec.kind),
            p.pos.x, p.pos.y, p.pos.z);
    }
    Sync();
    if (first >= 0) {
        g_select = Nodes::Find(NameChecksum(g_specs[first].name));
        Hud::Message(std::string("Added ") + what + " - its settings are in the Inspector; Ctrl+Z takes it away", 4.0);
    }
}

} // namespace

void Guard()
{
    if (g_boxes.empty() || !Layout::NetworkActive())
        return;
    Log("level editor: a network session - the editor's %u trigger volumes taken out (edited levels are offline only)",
        static_cast<unsigned>(g_boxes.size()));
    DropBoxes();
}

const std::vector<LevelTrigger>& LevelTriggers() { return g_level; }
const std::vector<Added>& AddedTriggers() { return g_added; }

bool IsTrigger(uint32_t name)
{
    return std::any_of(g_specs.begin(), g_specs.end(), [&](const Spec& s) { return NameChecksum(s.name) == name; });
}

bool Shown()
{
    if (!g_settingRead) {
        g_settingRead = true;
        g_shown = ConfigInt("editor", "triggers", 1) != 0;
    }
    return g_shown;
}

void SetShown(bool shown)
{
    Shown();
    g_shown = shown;
    ConfigSetInt("editor", "triggers", shown ? 1 : 0);
}

uint32_t InspectedSector() { return g_inspected; }
void Inspect(uint32_t sector) { g_inspected = sector; }

void Queue(std::function<void()> action) { g_actions.push_back(std::move(action)); }

void Start()
{
    NoteLevel();
    ScanLevel();
    g_levelGapsRead = false;
    Sync();
}

void Stop() { g_actions.clear(); }

void Tick(V3 camera, V3 forward, float yaw)
{
    g_camera = camera;
    g_forward = forward;
    g_yaw = yaw;
    NoteLevel();
    if (!g_scanned)
        ScanLevel();
    std::vector<std::function<void()>> actions;
    actions.swap(g_actions);
    for (auto& action : actions)
        action();
    Sync();
}

// --- the UI's actions (level_editor_triggers_ui.cpp queues them) ---

void AddKill(bool water)
{
    Placed p;
    p.spec.name = NodeCreate::FreshName("ed_trigger", NameTaken);
    p.spec.kind = Kind::Kill;
    p.spec.water = water;
    p.spec.size = water ? V3{800.0f, 100.0f, 800.0f} : V3{400.0f, 200.0f, 400.0f};
    p.pos = PlaceAhead(p.spec.size, 0.0f);
    p.yaw = g_yaw;
    p.spec.target = NearestRestart(p.pos);
    Create({p}, water ? "a water kill zone" : "a kill zone");
}

void AddTeleport()
{
    Placed p;
    p.spec.name = NodeCreate::FreshName("ed_trigger", NameTaken);
    p.spec.kind = Kind::Teleport;
    p.spec.size = {200.0f, 200.0f, 200.0f};
    p.pos = PlaceAhead(p.spec.size, 0.0f);
    p.yaw = g_yaw;
    p.spec.target = NearestRestart(p.pos);
    Create({p}, "a teleport");
}

void AddScript()
{
    Placed p;
    p.spec.name = NodeCreate::FreshName("ed_trigger", NameTaken);
    p.spec.kind = Kind::Script;
    p.spec.size = {300.0f, 200.0f, 300.0f};
    p.pos = PlaceAhead(p.spec.size, 0.0f);
    p.yaw = g_yaw;
    Create({p}, "a trigger that runs a script (choose it in the Inspector)");
}

void AddGap()
{
    Placed start, end;
    std::string gap = FreshGap();
    start.spec.name = NodeCreate::FreshName("ed_trigger", NameTaken);
    start.spec.kind = Kind::GapStart;
    start.spec.size = {300.0f, 300.0f, 60.0f};
    start.spec.gap = gap;
    start.spec.flags = {"CANCEL_GROUND"};
    int number = std::atoi(gap.c_str() + 6);
    char text[32];
    std::snprintf(text, sizeof(text), "Editor Gap %d", number > 0 ? number : 1);
    start.spec.text = text;
    start.spec.score = 100;
    start.pos = PlaceAhead(start.spec.size, 0.0f);
    start.yaw = g_yaw;
    end = start;
    end.spec.name = "";
    end.spec.kind = Kind::GapEnd;
    // (the end 600 units further along the view, on the ground there)
    end.pos = PlaceAhead(end.spec.size, 600.0f);
    // (two fresh names: the first isn't taken until it's made)
    std::string first = start.spec.name;
    end.spec.name = NodeCreate::FreshName("ed_trigger", [&](const std::string& n) { return SameText(n, first) || NameTaken(n); });
    Create({start, end}, "a gap: its start and its end");
}

void SetSpec(const std::string& name, const Spec& spec, bool undo)
{
    int i = SpecIndex(name);
    if (i < 0)
        return;
    if (undo)
        NoteUndo();
    Spec s = spec;
    s.name = g_specs[i].name;
    s.size = ClampSize(s.size);
    s.text = CleanText(s.text);
    // (a gap's name, score and flags are the same at both ends)
    if (s.kind == Kind::GapStart || s.kind == Kind::GapEnd) {
        for (Spec& other : g_specs) {
            if (&other != &g_specs[i] && !s.gap.empty() && SameText(other.gap, g_specs[i].gap) &&
                (other.kind == Kind::GapStart || other.kind == Kind::GapEnd)) {
                other.gap = s.gap;
                other.text = s.text;
                other.score = s.score;
                other.flags = s.flags;
            }
        }
    }
    g_specs[i] = s;
}

void BeginEdit() { NoteUndo(); }

void TurnOff(uint32_t sector, bool off)
{
    const LevelTrigger* t = nullptr;
    for (const LevelTrigger& l : g_level) {
        if (l.sector == sector)
            t = &l;
    }
    if (!t || !(t->wall || t->allTrigger)) {
        Hud::Message("Its collision has faces that aren't a trigger's - switching it off would make them go too", 5.0);
        return;
    }
    if (std::find(g_switched.begin(), g_switched.end(), sector) == g_switched.end())
        g_switched.push_back(sector); // (its wall state in the undo steps from now on)
    NoteUndo();
    if (!Walls::SetOff(sector, off)) {
        Hud::Message("Couldn't switch it", 4.0);
        return;
    }
    Hud::Message(off ? "Trigger " + LabelOf(sector) + " switched off (a `wall` record in the layout; Ctrl+Z switches it on)"
                     : "Trigger " + LabelOf(sector) + " switched on again");
}

void SelectWall(uint32_t sector)
{
    g_selectWall = sector;
}

void MakeEditable(uint32_t sector)
{
    const LevelTrigger* t = nullptr;
    for (const LevelTrigger& l : g_level) {
        if (l.sector == sector)
            t = &l;
    }
    if (!t)
        return;
    if (!t->script) {
        Hud::Message("It has no TriggerScript to copy", 4.0);
        return;
    }
    if (!t->wall && !t->allTrigger) {
        Hud::Message("Its collision has faces that aren't a trigger's - they'd go with it while the copy stands in", 5.0);
        return;
    }
    for (const Spec& s : g_specs) {
        if (s.replaces == sector && NodeAlive(s.name)) {
            Hud::Message("It has an editable copy already: " + s.name, 4.0);
            return;
        }
    }
    Placed p;
    p.spec.name = NodeCreate::FreshName("ed_trigger", NameTaken);
    p.spec.kind = Kind::Script;
    p.spec.script = NameFor(t->script, Script::KeptQbName(t->script));
    p.spec.replaces = sector;
    p.spec.size = ClampSize({std::max(20.0f, t->max.x - t->min.x), std::max(20.0f, t->max.y - t->min.y),
                             std::max(20.0f, t->max.z - t->min.z)});
    p.pos = (t->min + t->max) * 0.5f;
    p.yaw = 0.0f;
    Create({p}, ("an editable copy of " + LabelOf(sector) + " (the original is off while it's there)").c_str());
}

void SelectPartner(const std::string& name)
{
    int i = SpecIndex(name);
    int other = Partner(g_specs, i);
    if (other >= 0)
        g_select = Nodes::Find(NameChecksum(g_specs[other].name));
}

void SelectNodeNamed(uint32_t name)
{
    int index = Nodes::Find(name);
    if (index >= 0)
        g_select = index;
}

// --- picking and drawing ---

bool Pick(V3 origin, V3 unitDir, int* node, uint32_t* sector)
{
    *node = -1;
    *sector = 0;
    if (!Shown())
        return false;
    float best = 1e30f;
    bool any = false;
    for (const Added& a : g_added) {
        if (!a.alive)
            continue;
        OrientedBox box = BoxOf(a.pos, a.yaw, a.spec.size);
        float t;
        if (RayBox(origin, unitDir, box.origin, box.axes, box.min, box.max, &t) && t < best) {
            best = t;
            *node = Nodes::Find(NameChecksum(a.spec.name));
            *sector = 0;
            any = *node >= 0;
        }
    }
    for (const LevelTrigger& l : g_level) {
        V3 axes[3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
        V3 grow = {2.0f, 2.0f, 2.0f};
        float t;
        if (!RayBox(origin, unitDir, {0.0f, 0.0f, 0.0f}, axes, l.min - grow, l.max + grow, &t) || t >= best)
            continue;
        // (the faces themselves when there are few; a big water plane by its box)
        float hit = t;
        bool found = l.faceCount > kMaxFacesKept;
        for (size_t f = 0; !found && f + 2 < l.faces.size(); f += 3) {
            float ft;
            if (Picking::RayTriangle(origin, unitDir, l.faces[f], l.faces[f + 1], l.faces[f + 2], &ft) && ft < best) {
                hit = ft;
                found = true;
            }
        }
        if (!found || hit >= best)
            continue;
        best = hit;
        *node = l.hasNode ? Nodes::Find(l.sector) : -1;
        *sector = l.sector;
        any = true;
    }
    if (any && *sector)
        g_inspected = *sector;
    return any;
}

namespace {

struct Painter {
    V3 camera;
    const Frame* frame;
    const Lens* lens;
    const uint32_t* viewport;
    float nearZ;
    std::vector<Ui::Line>* lines;

    bool ToPixel(V3 p, float* x, float* y) const
    {
        float nx, ny;
        if (!Project(camera, *frame, *lens, p, nearZ, &nx, &ny))
            return false;
        NdcToPixel(viewport, nx, ny, x, y);
        return true;
    }
    void Line(V3 a, V3 b, uint32_t argb, float thickness = 1.5f) const
    {
        if (!ClipToNear(camera, *frame, nearZ + 0.01f, &a, &b))
            return;
        float ax, ay, bx, by;
        if (ToPixel(a, &ax, &ay) && ToPixel(b, &bx, &by))
            lines->push_back({ax, ay, bx, by, argb, thickness});
    }
    void Box(const OrientedBox& box, uint32_t argb, float thickness) const
    {
        V3 c[8];
        BoxCorners(box.origin, box.axes, box.min, box.max, c);
        for (int i = 0; i < 8; ++i) {
            for (int bit : {1, 2, 4}) {
                if (!(i & bit))
                    Line(c[i], c[i | bit], argb, thickness);
            }
        }
    }
    // An arrow from a to b, its head drawn on the screen.
    void Arrow(V3 a, V3 b, uint32_t argb) const
    {
        Line(a, b, argb, 2.0f);
        float ax, ay, bx, by;
        if (!ToPixel(a, &ax, &ay) || !ToPixel(b, &bx, &by))
            return;
        float dx = bx - ax, dy = by - ay, length = std::sqrt(dx * dx + dy * dy);
        if (length < 4.0f)
            return;
        dx /= length;
        dy /= length;
        float head = std::min(14.0f, length * 0.4f);
        for (float side : {-1.0f, 1.0f}) {
            float hx = -dx * 0.9f + side * -dy * 0.45f, hy = -dy * 0.9f + side * dx * 0.45f;
            lines->push_back({bx, by, bx + hx * head, by + hy * head, argb, 2.0f});
        }
    }
};

uint32_t Dim(uint32_t argb, uint32_t alpha) { return (argb & 0x00FFFFFFu) | (alpha << 24); }

float DistanceTo(V3 p, V3 min, V3 max)
{
    float dx = std::max({min.x - p.x, 0.0f, p.x - max.x}), dy = std::max({min.y - p.y, 0.0f, p.y - max.y}),
          dz = std::max({min.z - p.z, 0.0f, p.z - max.z});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::string Describe(const Calls& calls, Kind kind)
{
    std::string text = KindName(kind);
    switch (kind) {
    case Kind::Kill:
        if (calls.water)
            text += " (water)";
        if (calls.target)
            text += " -> " + LabelOf(calls.target);
        break;
    case Kind::Teleport:
        if (calls.target)
            text += " -> " + LabelOf(calls.target);
        break;
    case Kind::GapEnd:
        text += ": " + (calls.gapText.empty() ? LabelOf(calls.gapId) : calls.gapText);
        if (calls.gapScore)
            text += " (" + std::to_string(calls.gapScore) + ")";
        break;
    case Kind::GapStart:
        if (calls.gapId)
            text += ": " + LabelOf(calls.gapId);
        break;
    default:
        break;
    }
    return text;
}

bool NodePos(uint32_t name, V3* pos)
{
    Mth::Vector p;
    if (!name || !SkateScript::GetNode(name, false) || !SkateScript::GetPosition(name, &p))
        return false;
    *pos = {p.x, p.y, p.z};
    return true;
}

} // namespace

void AddOverlay(V3 camera, const Frame& frame, const Lens& lens, const uint32_t viewport[4], float nearZ,
                std::vector<Ui::Line>* lines, std::vector<Ui::Overlay::Text>* texts)
{
    if (!Shown())
        return;
    Painter paint = {camera, &frame, &lens, viewport, nearZ, lines};
    int labels = 0;
    auto label = [&](V3 at, uint32_t argb, const std::string& text) {
        if (labels >= kMaxLabels)
            return;
        float x, y;
        if (paint.ToPixel(at, &x, &y)) {
            texts->push_back({x, y, argb, text});
            ++labels;
        }
    };
    // The level's: nearest first.
    std::vector<std::pair<float, const LevelTrigger*>> near;
    for (const LevelTrigger& l : g_level) {
        float d = DistanceTo(camera, l.min, l.max);
        if (d <= kDrawRadius)
            near.push_back({d, &l});
    }
    std::sort(near.begin(), near.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    if (near.size() > static_cast<size_t>(kMaxLevelDrawn))
        near.resize(kMaxLevelDrawn);
    for (const auto& n : near) {
        const LevelTrigger& l = *n.second;
        bool inspected = l.sector == g_inspected;
        uint32_t colour = l.off ? 0x70909090u : Dim(KindColour(l.kind), inspected ? 0xFFu : 0xC0u);
        float thickness = inspected ? 2.5f : 1.0f;
        if (l.faceCount <= kMaxFacesKept) {
            for (size_t f = 0; f + 2 < l.faces.size(); f += 3) {
                paint.Line(l.faces[f], l.faces[f + 1], colour, thickness);
                paint.Line(l.faces[f + 1], l.faces[f + 2], colour, thickness);
                paint.Line(l.faces[f + 2], l.faces[f], colour, thickness);
            }
        } else {
            OrientedBox box = {{0.0f, 0.0f, 0.0f}, {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}, l.min, l.max};
            paint.Box(box, colour, thickness);
        }
        V3 centre = (l.min + l.max) * 0.5f;
        V3 target;
        if (!l.off && l.kind == Kind::Teleport && NodePos(l.calls.target, &target))
            paint.Arrow(centre, target, Dim(KindColour(l.kind), 0xE0));
        if (n.first <= kLabelRadius || inspected)
            label({centre.x, l.max.y, centre.z}, colour,
                  (l.off ? "(off) " : "") + Describe(l.calls, l.kind) + "  " + LabelOf(l.sector));
    }
    // The editor's.
    for (const Added& a : g_added) {
        if (!a.alive)
            continue;
        OrientedBox box = BoxOf(a.pos, a.yaw, a.spec.size);
        uint32_t colour = a.problem ? 0xC0A0A0A0u : KindColour(a.spec.kind);
        paint.Box(box, colour, 2.0f);
        V3 target;
        if (!a.problem && (a.spec.kind == Kind::Teleport || a.spec.kind == Kind::Kill) &&
            NodePos(ChecksumOf(a.spec.target), &target))
            paint.Arrow(a.pos, target, Dim(colour, a.spec.kind == Kind::Kill ? 0x80 : 0xE0));
        if (a.spec.kind == Kind::GapStart) {
            int other = Partner(g_specs, SpecIndex(a.spec.name));
            if (other >= 0) {
                for (const Added& b : g_added) {
                    if (b.alive && SameText(b.spec.name, g_specs[other].name))
                        paint.Arrow(a.pos, b.pos, Dim(KindColour(Kind::GapEnd), 0xB0));
                }
            }
        }
        std::string text = a.spec.name + ": " + KindName(a.spec.kind);
        if (a.spec.kind == Kind::Kill || a.spec.kind == Kind::Teleport)
            text += " -> " + a.spec.target;
        else if (a.spec.kind == Kind::Script)
            text += " " + a.spec.script;
        else if (a.spec.kind == Kind::GapStart || a.spec.kind == Kind::GapEnd)
            text += " \"" + a.spec.text + "\"";
        if (a.problem)
            text += " (not active)";
        label({a.pos.x, a.pos.y + a.spec.size.y * 0.5f, a.pos.z}, colour, text);
    }
}

// --- undo ---

State Save()
{
    State state;
    state.specs = g_specs;
    for (const NodeCreate::Spec& n : NodeCreate::Current()) {
        if (n.owned && SpecIndex(n.name) >= 0)
            state.nodes.push_back(n);
    }
    if (!g_switched.empty())
        state.walls = Walls::Save(g_switched);
    return state;
}

void Restore(const State& state)
{
    // The trigger nodes there now that the state hasn't: removed; those it has: made, or put where it had them.
    for (const NodeCreate::Spec& n : NodeCreate::Current()) {
        if (!n.owned || SpecIndex(n.name) < 0)
            continue;
        bool keep = std::any_of(state.nodes.begin(), state.nodes.end(), [&](const NodeCreate::Spec& s) { return SameText(s.name, n.name); });
        if (!keep) {
            NodeCreate::Spec removed;
            int at;
            NodeCreate::Remove(NameChecksum(n.name), &removed, &at);
        }
    }
    g_specs = state.specs;
    std::string message;
    for (const NodeCreate::Spec& n : state.nodes) {
        uint32_t name = NameChecksum(n.name);
        if (!NodeCreate::IsAdded(name)) {
            NodeCreate::Create(n, -1, &message);
        } else {
            NodeEdit::Values values;
            if (NodeEdit::Read(name, &values) &&
                (values.pos.x != n.pos.x || values.pos.y != n.pos.y || values.pos.z != n.pos.z || values.angles.y != n.angles.y)) {
                values.pos = n.pos;
                values.angles = n.angles;
                NodeEdit::Set(name, values);
            }
        }
    }
    if (!state.walls.empty())
        Walls::ApplyStates(state.walls);
    g_hasUndo = false;
    Sync();
}

bool TakeUndo(State* before)
{
    if (!g_hasUndo)
        return false;
    g_hasUndo = false;
    *before = std::move(g_undoBefore);
    g_undoBefore = State{};
    return true;
}

int TakeSelect()
{
    int select = g_select;
    g_select = -1;
    return select;
}

uint32_t TakeWallSelect()
{
    uint32_t wall = g_selectWall;
    g_selectWall = 0;
    return wall;
}

// --- the layout ---

std::vector<Placed> CaptureAdded()
{
    std::vector<Placed> out;
    for (const Spec& spec : g_specs) {
        Placed p;
        p.spec = spec;
        if (NodeAlive(spec.name) && NodePlace(spec.name, &p.pos, &p.yaw))
            out.push_back(p);
    }
    return out;
}

int Apply(const std::vector<Placed>& placed, std::string* message)
{
    NoteLevel();
    if (!g_scanned)
        ScanLevel();
    int made = 0;
    for (const Placed& p : placed) {
        if (SpecIndex(p.spec.name) >= 0 || NodeCreate::NameTaken(p.spec.name)) {
            Log("level editor: the layout's trigger %s is there already - skipped", p.spec.name.c_str());
            continue;
        }
        if (!NodeCreate::Create(NodeSpec(p.spec, p.pos, p.yaw), -1, message))
            continue;
        g_specs.push_back(p.spec);
        ++made;
    }
    Sync();
    return made;
}

void ResetAll()
{
    for (const Spec& spec : std::vector<Spec>(g_specs)) {
        NodeCreate::Spec removed;
        int at;
        NodeCreate::Remove(NameChecksum(spec.name), &removed, &at);
    }
    g_specs.clear();
    g_switched.clear();
    g_inspected = 0;
    g_hasUndo = false;
    Sync();
}

std::string Describe()
{
    int alive = 0;
    for (const Added& a : g_added)
        alive += a.alive ? 1 : 0;
    int off = 0;
    for (const LevelTrigger& l : g_level)
        off += l.off ? 1 : 0;
    char text[160];
    std::snprintf(text, sizeof(text), "%u level trigger volumes (%d off), %d added", static_cast<unsigned>(g_level.size()),
                  off, alive);
    return text;
}

std::string DescribeLevelTrigger(const LevelTrigger& l) { return Describe(l.calls, l.kind); }

} // namespace Front::LevelEditor::Triggers
