// NOT ORIGINAL - the level editor's chains: grind rails, ladders and paths (ours). See level_editor_chains.h.

#include "game/front/level_editor_chains.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <unordered_map>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/front/level_editor_fields.h"
#include "game/front/level_editor_layout.h"
#include "game/front/level_editor_node_create.h"
#include "game/front/level_editor_node_edit.h"
#include "game/front/level_editor_nodes.h"
#include "game/front/level_editor_objects.h"
#include "game/front/level_editor_scenery.h"
#include "game/front/level_editor_triggers.h"
#include "game/lst/hashtable.h"
#include "game/obj/object.h"
#include "game/obj/rail_manager.h"
#include "game/obj/trick_objects.h"
#include "game/script/load.h"
#include "game/script/node_manager.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/log.h"

namespace Front::LevelEditor::Chains {

// --- the pure parts ---------------------------------------------------------------------------------------------

namespace {

bool SameText(const std::string& a, const char* b)
{
    if (a.size() != std::strlen(b))
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

bool SameV3(V3 a, V3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

void Hex(std::ostringstream& out, uint32_t value)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%08X", value);
    out << text;
}

bool ParseHex(const std::string& text, uint32_t* value)
{
    if (text.size() != 8)
        return false;
    for (char c : text) {
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
    }
    *value = static_cast<uint32_t>(std::strtoul(text.c_str(), nullptr, 16));
    return true;
}

bool ReadHexWord(std::istringstream& in, uint32_t* value)
{
    std::string word;
    return static_cast<bool>(in >> word) && ParseHex(word, value);
}

// "a,b,c" of 8-hex-digit names (none: false).
bool ParseNames(const std::string& text, std::vector<uint32_t>* out)
{
    out->clear();
    size_t start = 0;
    while (start <= text.size()) {
        size_t comma = text.find(',', start);
        std::string word = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        uint32_t value;
        if (!ParseHex(word, &value))
            return false;
        out->push_back(value);
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return !out->empty();
}

void Names(std::ostringstream& out, const std::vector<uint32_t>& names)
{
    for (size_t i = 0; i < names.size(); ++i) {
        if (i)
            out << ',';
        Hex(out, names[i]);
    }
}

bool ValidWord(const std::string& name)
{
    if (name.empty() || name.size() > 63)
        return false;
    for (char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
            return false;
    }
    return true;
}

bool ReadTriple(const std::string& text, V3* out)
{
    float v[3];
    const char* at = text.c_str();
    for (int i = 0; i < 3; ++i) {
        char* end = nullptr;
        v[i] = std::strtof(at, &end);
        if (end == at || !std::isfinite(v[i]))
            return false;
        at = end;
        if (i < 2) {
            if (*at != ',')
                return false;
            ++at;
        }
    }
    if (*at != '\0')
        return false;
    *out = {v[0], v[1], v[2]};
    return true;
}

bool ReadFloat(const std::string& text, float* out)
{
    char* end = nullptr;
    float v = std::strtof(text.c_str(), &end);
    if (text.empty() || end != text.c_str() + text.size() || !std::isfinite(v))
        return false;
    *out = v;
    return true;
}

template <typename T> void SortByName(std::vector<T>* list)
{
    std::sort(list->begin(), list->end(), [](const T& a, const T& b) { return a.name < b.name; });
}

} // namespace

bool SamePoint(const Point& a, const Point& b)
{
    return a.name == b.name && a.cls == b.cls && SameV3(a.pos, b.pos) && a.like == b.like && a.terrain == b.terrain &&
           a.links == b.links && a.facing == b.facing && a.cluster == b.cluster && a.grindFlat == b.grindFlat &&
           SameText(a.type, b.type.c_str()) && a.hang == b.hang && a.speed == b.speed;
}

bool SameState(const State& a, const State& b)
{
    if (a.points.size() != b.points.size() || a.links.size() != b.links.size() || a.dropped != b.dropped ||
        a.terrains.size() != b.terrains.size() || a.facings.size() != b.facings.size() || a.grindFlat != b.grindFlat ||
        a.speeds.size() != b.speeds.size())
        return false;
    for (size_t i = 0; i < a.speeds.size(); ++i) {
        if (a.speeds[i].name != b.speeds[i].name || a.speeds[i].rate != b.speeds[i].rate)
            return false;
    }
    for (size_t i = 0; i < a.facings.size(); ++i) {
        if (a.facings[i].name != b.facings[i].name || a.facings[i].facing != b.facings[i].facing)
            return false;
    }
    for (size_t i = 0; i < a.points.size(); ++i) {
        if (!SamePoint(a.points[i], b.points[i]))
            return false;
    }
    for (size_t i = 0; i < a.links.size(); ++i) {
        if (a.links[i].name != b.links[i].name || a.links[i].links != b.links[i].links)
            return false;
    }
    for (size_t i = 0; i < a.terrains.size(); ++i) {
        if (a.terrains[i].name != b.terrains[i].name || a.terrains[i].terrain != b.terrains[i].terrain)
            return false;
    }
    return true;
}

bool Empty(const State& state)
{
    return state.points.empty() && state.links.empty() && state.dropped.empty() && state.terrains.empty() &&
           state.facings.empty() && state.grindFlat.empty() && state.speeds.empty();
}

bool IsChainClass(const std::string& cls)
{
    return SameText(cls, "RailNode") || SameText(cls, "Waypoint") || SameText(cls, "ClimbingNode");
}

std::string WriteRecords(const State& state)
{
    std::ostringstream out;
    for (const Point& p : state.points) {
        char pos[96];
        std::snprintf(pos, sizeof(pos), " pos=%.9g,%.9g,%.9g", p.pos.x, p.pos.y, p.pos.z);
        out << "point " << p.name << ' ' << p.cls << pos;
        if (p.like) {
            out << " like=";
            Hex(out, p.like);
        }
        if (!p.terrain.empty())
            out << " terrain=" << p.terrain;
        if (!p.links.empty()) {
            out << " links=";
            Names(out, p.links);
        }
        if (SameText(p.cls, "ClimbingNode") || p.facing != 0.0f) {
            char facing[48];
            std::snprintf(facing, sizeof(facing), " facing=%.9g", p.facing);
            out << facing;
        }
        if (p.cluster) {
            out << " cluster=";
            Hex(out, p.cluster);
        }
        if (p.grindFlat)
            out << " grindflat=1";
        if (IsLedge(p))
            out << " type=Ledge";
        if (p.hang)
            out << " hang=" << HangWord(p.hang);
        if (p.speed > 0.0f) {
            char speed[48];
            std::snprintf(speed, sizeof(speed), " speed=%.9g", p.speed);
            out << speed;
        }
        out << '\n';
    }
    for (const LinkEdit& l : state.links) {
        out << "links ";
        Hex(out, l.name);
        out << ' ';
        if (l.links.empty())
            out << '-';
        else
            Names(out, l.links);
        out << '\n';
    }
    for (uint32_t name : state.dropped) {
        out << "drop ";
        Hex(out, name);
        out << '\n';
    }
    for (const TerrainEdit& t : state.terrains) {
        out << "terrain ";
        Hex(out, t.name);
        out << ' ' << t.terrain << '\n';
    }
    for (const FacingEdit& f : state.facings) {
        char facing[48];
        std::snprintf(facing, sizeof(facing), " %.9g", f.facing);
        out << "facing ";
        Hex(out, f.name);
        out << facing << '\n';
    }
    for (uint32_t name : state.grindFlat) {
        out << "grindflat ";
        Hex(out, name);
        out << '\n';
    }
    for (const SpeedEdit& e : state.speeds) {
        char rate[48];
        std::snprintf(rate, sizeof(rate), " %.9g", e.rate);
        out << "railspeed ";
        Hex(out, e.name);
        out << rate << '\n';
    }
    return out.str();
}

bool IsRecord(const std::string& kind)
{
    return kind == "point" || kind == "links" || kind == "drop" || kind == "terrain" || kind == "facing" ||
           kind == "grindflat" || kind == "railspeed";
}

bool ReadRecord(const std::string& kind, std::istringstream& in, State* state)
{
    if (kind == "point") {
        Point p;
        if (!(in >> p.name >> p.cls) || !ValidWord(p.name) || !IsChainClass(p.cls))
            return false;
        std::vector<std::string> seen;
        std::string field;
        while (in >> field) {
            size_t equals = field.find('=');
            if (equals == std::string::npos || equals == 0)
                return false;
            std::string key = field.substr(0, equals), value = field.substr(equals + 1);
            if (std::find(seen.begin(), seen.end(), key) != seen.end())
                return false;
            seen.push_back(key);
            if (key == "pos") {
                if (!ReadTriple(value, &p.pos))
                    return false;
            } else if (key == "like") {
                if (!ParseHex(value, &p.like))
                    return false;
            } else if (key == "terrain") {
                if (!ValidWord(value))
                    return false;
                p.terrain = value;
            } else if (key == "links") {
                if (!ParseNames(value, &p.links))
                    return false;
            } else if (key == "facing") {
                if (!ReadFloat(value, &p.facing))
                    return false;
            } else if (key == "cluster") {
                if (!ParseHex(value, &p.cluster))
                    return false;
            } else if (key == "grindflat") {
                if (value != "0" && value != "1")
                    return false;
                p.grindFlat = value == "1";
            } else if (key == "type") {
                // (a ClimbingNode's: Ledge - a ladder's Type isn't written)
                if (!SameText(value, "Ledge") || !SameText(p.cls, "ClimbingNode"))
                    return false;
                p.type = "Ledge";
            } else if (key == "hang") {
                if (value == HangWord(kHangLeft))
                    p.hang = kHangLeft;
                else if (value == HangWord(kHangRight))
                    p.hang = kHangRight;
                else
                    return false;
            } else if (key == "speed") {
                if (!ReadFloat(value, &p.speed) || !(p.speed >= kSpeedMin && p.speed <= kSpeedMax) ||
                    !SameText(p.cls, "RailNode"))
                    return false;
            } // (others: a later version's - skipped)
        }
        if (std::find(seen.begin(), seen.end(), "pos") == seen.end())
            return false;
        for (const Point& q : state->points) {
            if (SameText(q.name, p.name.c_str()))
                return false;
        }
        state->points.push_back(p);
        return true;
    }
    if (kind == "links") {
        LinkEdit l = {};
        std::string value;
        if (!ReadHexWord(in, &l.name) || !(in >> value))
            return false;
        if (value != "-" && !ParseNames(value, &l.links))
            return false;
        for (LinkEdit& other : state->links) {
            if (other.name == l.name)
                return false;
        }
        state->links.push_back(l);
        SortByName(&state->links);
        return true;
    }
    if (kind == "drop") {
        uint32_t name;
        if (!ReadHexWord(in, &name))
            return false;
        if (std::find(state->dropped.begin(), state->dropped.end(), name) == state->dropped.end()) {
            state->dropped.push_back(name);
            std::sort(state->dropped.begin(), state->dropped.end());
        }
        return true;
    }
    if (kind == "terrain") {
        TerrainEdit t = {};
        if (!ReadHexWord(in, &t.name) || !(in >> t.terrain) || !ValidWord(t.terrain))
            return false;
        for (TerrainEdit& other : state->terrains) {
            if (other.name == t.name)
                return false;
        }
        state->terrains.push_back(t);
        SortByName(&state->terrains);
        return true;
    }
    if (kind == "facing") {
        FacingEdit f = {};
        std::string value;
        if (!ReadHexWord(in, &f.name) || !(in >> value) || !ReadFloat(value, &f.facing))
            return false;
        for (FacingEdit& other : state->facings) {
            if (other.name == f.name)
                return false;
        }
        state->facings.push_back(f);
        SortByName(&state->facings);
        return true;
    }
    if (kind == "grindflat") {
        uint32_t name;
        if (!ReadHexWord(in, &name))
            return false;
        if (std::find(state->grindFlat.begin(), state->grindFlat.end(), name) == state->grindFlat.end()) {
            state->grindFlat.push_back(name);
            std::sort(state->grindFlat.begin(), state->grindFlat.end());
        }
        return true;
    }
    if (kind == "railspeed") {
        SpeedEdit e = {};
        std::string value;
        if (!ReadHexWord(in, &e.name) || !(in >> value) || !ReadFloat(value, &e.rate) ||
            !(e.rate >= kSpeedMin && e.rate <= kSpeedMax))
            return false;
        for (const SpeedEdit& other : state->speeds) {
            if (other.name == e.name)
                return false;
        }
        state->speeds.push_back(e);
        SortByName(&state->speeds);
        return true;
    }
    return false;
}

float ClampSpeed(float rate)
{
    if (!(rate >= kSpeedMin))
        return kSpeedMin;
    return rate > kSpeedMax ? kSpeedMax : rate;
}

bool IsLedge(const Point& point) { return SameText(point.cls, "ClimbingNode") && SameText(point.type, "Ledge"); }

const char* HangWord(int hang) { return hang == kHangLeft ? "left" : hang == kHangRight ? "right" : ""; }

const std::vector<uint8_t>& RailSpeedQb()
{
    // (NeverScript's output for the source in level_editor_chains.h - the self-test walks it token by token)
    static const std::vector<uint8_t> qb = {
        0x23, 0x16, 0x7D, 0x9E, 0xC1, 0xF6, 0x16, 0xC6, 0xC0, 0x13, 0x20, 0x07, 0x1A, 0xCD, 0xCC, 0x4C,
        0x3D, 0x01, 0x47, 0x4C, 0x00, 0x16, 0x82, 0x73, 0x71, 0x3A, 0x05, 0x17, 0x03, 0x00, 0x00, 0x00,
        0x09, 0x17, 0x0D, 0x00, 0x00, 0x00, 0x09, 0x17, 0xFF, 0xFF, 0xFF, 0xFF, 0x06, 0x01, 0x16, 0x2D,
        0xC0, 0x0D, 0xCD, 0x16, 0xF9, 0x81, 0xDC, 0xA1, 0x07, 0x16, 0x6F, 0x9E, 0xFC, 0x68, 0x01, 0x16,
        0xC2, 0xE5, 0xA4, 0x23, 0x16, 0x6F, 0x9E, 0xFC, 0x68, 0x16, 0x0C, 0xF1, 0x31, 0x70, 0x07, 0x03,
        0x16, 0xC6, 0xC0, 0x13, 0x20, 0x07, 0x2D, 0x16, 0xC6, 0xC0, 0x13, 0x20, 0x04, 0x01, 0x28, 0x01,
        0x24, 0x01, 0x23, 0x16, 0x6F, 0x9E, 0xFC, 0x68, 0x16, 0xC6, 0xC0, 0x13, 0x20, 0x07, 0x1A, 0xCD,
        0xCC, 0x4C, 0x3D, 0x01, 0x16, 0x75, 0x26, 0x81, 0x20, 0x07, 0x17, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x20, 0x01, 0x47, 0x83, 0x00, 0x16, 0x62, 0x1E, 0x85, 0xE9, 0x01, 0x16, 0x2D, 0xAD, 0x0A, 0xE9,
        0x01, 0x16, 0xD0, 0x6F, 0x61, 0xA4, 0x07, 0x0E, 0x2D, 0x16, 0x53, 0x19, 0x26, 0x7F, 0x08, 0x1E,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x01, 0x47, 0x36,
        0x00, 0x0E, 0x2D, 0x16, 0xD0, 0x6F, 0x61, 0xA4, 0x14, 0x2D, 0x16, 0x75, 0x26, 0x81, 0x20, 0x0F,
        0x01, 0x16, 0x4A, 0xAC, 0xCA, 0xC0, 0x01, 0x16, 0x9B, 0x93, 0x3B, 0x38, 0x0E, 0x2D, 0x16, 0x09,
        0x01, 0xD9, 0xF0, 0x0D, 0x0E, 0x1A, 0x00, 0x00, 0x80, 0x3F, 0x0B, 0x2D, 0x16, 0xC6, 0xC0, 0x13,
        0x20, 0x0F, 0x0F, 0x01, 0x28, 0x01, 0x16, 0x75, 0x26, 0x81, 0x20, 0x07, 0x2D, 0x16, 0xD0, 0x6F,
        0x61, 0xA4, 0x01, 0x16, 0x1A, 0x7C, 0x11, 0x82, 0x17, 0x01, 0x00, 0x00, 0x00, 0x16, 0x05, 0xDF,
        0x37, 0xFC, 0x01, 0x48, 0x06, 0x00, 0x01, 0x22, 0x01, 0x28, 0x01, 0x21, 0x01, 0x24, 0x01, 0x2B,
        0x7D, 0x9E, 0xC1, 0xF6, 0x65, 0x64, 0x5F, 0x52, 0x61, 0x69, 0x6C, 0x53, 0x70, 0x65, 0x65, 0x64,
        0x00, 0x2B, 0xF9, 0x81, 0xDC, 0xA1, 0x6E, 0x61, 0x6D, 0x65, 0x00, 0x2B, 0xC2, 0xE5, 0xA4, 0x23,
        0x4F, 0x62, 0x6A, 0x5F, 0x53, 0x70, 0x61, 0x77, 0x6E, 0x53, 0x63, 0x72, 0x69, 0x70, 0x74, 0x00,
        0x2B, 0x9B, 0x93, 0x3B, 0x38, 0x53, 0x65, 0x74, 0x53, 0x70, 0x65, 0x65, 0x64, 0x00, 0x2B, 0x09,
        0x01, 0xD9, 0xF0, 0x73, 0x70, 0x65, 0x65, 0x64, 0x00, 0x2B, 0x75, 0x26, 0x81, 0x20, 0x4F, 0x6C,
        0x64, 0x50, 0x6F, 0x73, 0x00, 0x2B, 0x62, 0x1E, 0x85, 0xE9, 0x4F, 0x6E, 0x52, 0x61, 0x69, 0x6C,
        0x00, 0x2B, 0x4A, 0xAC, 0xCA, 0xC0, 0x47, 0x65, 0x74, 0x53, 0x70, 0x65, 0x65, 0x64, 0x00, 0x2B,
        0xC6, 0xC0, 0x13, 0x20, 0x72, 0x61, 0x74, 0x65, 0x00, 0x2B, 0x2D, 0xC0, 0x0D, 0xCD, 0x4B, 0x69,
        0x6C, 0x6C, 0x53, 0x70, 0x61, 0x77, 0x6E, 0x65, 0x64, 0x53, 0x63, 0x72, 0x69, 0x70, 0x74, 0x00,
        0x2B, 0x6F, 0x9E, 0xFC, 0x68, 0x65, 0x64, 0x5F, 0x52, 0x61, 0x69, 0x6C, 0x53, 0x70, 0x65, 0x65,
        0x64, 0x4C, 0x6F, 0x6F, 0x70, 0x00, 0x2B, 0xD0, 0x6F, 0x61, 0xA4, 0x4E, 0x65, 0x77, 0x50, 0x6F,
        0x73, 0x00, 0x2B, 0x82, 0x73, 0x71, 0x3A, 0x54, 0x72, 0x69, 0x67, 0x67, 0x65, 0x72, 0x54, 0x79,
        0x70, 0x65, 0x00, 0x2B, 0x0C, 0xF1, 0x31, 0x70, 0x70, 0x61, 0x72, 0x61, 0x6D, 0x73, 0x00, 0x2B,
        0x2D, 0xAD, 0x0A, 0xE9, 0x4F, 0x62, 0x6A, 0x5F, 0x47, 0x65, 0x74, 0x50, 0x6F, 0x73, 0x69, 0x74,
        0x69, 0x6F, 0x6E, 0x00, 0x2B, 0x53, 0x19, 0x26, 0x7F, 0x50, 0x6F, 0x73, 0x00, 0x2B, 0x1A, 0x7C,
        0x11, 0x82, 0x77, 0x61, 0x69, 0x74, 0x00, 0x2B, 0x05, 0xDF, 0x37, 0xFC, 0x67, 0x61, 0x6D, 0x65,
        0x66, 0x72, 0x61, 0x6D, 0x65, 0x00, 0x00,
    };
    return qb;
}

std::string SpeedScriptName(uint32_t node)
{
    char text[32];
    std::snprintf(text, sizeof(text), "ed_railspeed_%08X", node);
    return text;
}

namespace {
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
} // namespace

std::vector<uint8_t> SpeedBody(uint32_t original, float rate)
{
    std::vector<uint8_t> out;
    if (original) {
        PutName(&out, original);
        out.push_back(0x2C); // (<...>: its arguments passed on)
        out.push_back(0x01);
    }
    PutName(&out, Crc::GenerateCRCFromString("ed_RailSpeed"));
    PutName(&out, Crc::GenerateCRCFromString("rate"));
    out.push_back(0x07);
    out.push_back(0x1A);
    uint32_t bits;
    std::memcpy(&bits, &rate, 4);
    Put4(&out, bits);
    out.push_back(0x01);
    return out;
}

std::vector<uint8_t> SpeedQbFile(const std::vector<SpeedScript>& scripts)
{
    std::vector<uint8_t> out;
    for (const SpeedScript& s : scripts) {
        out.push_back(0x23);
        PutName(&out, Crc::GenerateCRCFromString(SpeedScriptName(s.node).c_str()));
        out.push_back(0x01);
        std::vector<uint8_t> body = SpeedBody(s.original, s.rate);
        out.insert(out.end(), body.begin(), body.end());
        out.push_back(0x24);
        out.push_back(0x01);
    }
    for (const SpeedScript& s : scripts) {
        std::string name = SpeedScriptName(s.node);
        out.push_back(0x2B);
        Put4(&out, Crc::GenerateCRCFromString(name.c_str()));
        out.insert(out.end(), name.begin(), name.end());
        out.push_back(0x00);
    }
    out.push_back(0x00);
    return out;
}

std::vector<uint32_t> Predecessors(const Graph& graph, uint32_t name)
{
    std::vector<uint32_t> found;
    for (const auto& entry : graph) {
        if (entry.first != name && std::find(entry.second.begin(), entry.second.end(), name) != entry.second.end())
            found.push_back(entry.first);
    }
    return found;
}

namespace {
const std::vector<uint32_t>& LinksOf(const Graph& graph, uint32_t name)
{
    static const std::vector<uint32_t> none;
    auto it = graph.find(name);
    return it == graph.end() ? none : it->second;
}
} // namespace

bool IsLast(const Graph& graph, uint32_t name) { return LinksOf(graph, name).empty(); }
bool IsFirst(const Graph& graph, uint32_t name) { return Predecessors(graph, name).empty(); }

bool Extend(Graph* graph, uint32_t from, uint32_t point)
{
    if (from == point || graph->count(point))
        return false;
    if (IsLast(*graph, from)) {
        (*graph)[from] = {point};
        (*graph)[point] = {};
        return true;
    }
    if (IsFirst(*graph, from)) {
        (*graph)[point] = {from};
        return true;
    }
    return false;
}

bool InsertAfter(Graph* graph, uint32_t from, uint32_t point)
{
    auto it = graph->find(from);
    if (it == graph->end() || it->second.empty() || from == point || graph->count(point))
        return false;
    uint32_t next = it->second.front();
    it->second.front() = point;
    (*graph)[point] = {next};
    return true;
}

void Remove(Graph* graph, uint32_t name)
{
    std::vector<uint32_t> own = LinksOf(*graph, name);
    uint32_t next = own.empty() ? 0 : own.front();
    for (auto& entry : *graph) {
        if (entry.first == name)
            continue;
        std::vector<uint32_t>& links = entry.second;
        auto at = std::find(links.begin(), links.end(), name);
        if (at == links.end())
            continue;
        bool already = next && std::find(links.begin(), links.end(), next) != links.end();
        if (next && !already && next != entry.first)
            *at = next;
        else
            links.erase(at);
    }
    (*graph)[name] = {};
}

bool Link(Graph* graph, uint32_t a, uint32_t b)
{
    if (a == b)
        return false;
    if (IsLast(*graph, a)) {
        (*graph)[a] = {b};
        if (!graph->count(b))
            (*graph)[b] = {};
        return true;
    }
    if (IsLast(*graph, b)) {
        (*graph)[b] = {a};
        return true;
    }
    return false;
}

bool Unlink(Graph* graph, uint32_t name)
{
    auto it = graph->find(name);
    if (it == graph->end() || it->second.empty())
        return false;
    it->second.erase(it->second.begin());
    return true;
}

std::vector<uint32_t> Connected(const Graph& graph, uint32_t name)
{
    std::vector<uint32_t> found = {name};
    for (size_t i = 0; i < found.size(); ++i) {
        uint32_t at = found[i];
        std::vector<uint32_t> near = LinksOf(graph, at);
        for (uint32_t p : Predecessors(graph, at))
            near.push_back(p);
        for (uint32_t n : near) {
            if (std::find(found.begin(), found.end(), n) == found.end())
                found.push_back(n);
        }
    }
    return found;
}

V3 SnapToEdge(V3 hit, V3 normal, float reach, const std::function<bool(V3 from, float* groundY)>& groundAt,
              bool* snapped)
{
    *snapped = false;
    if (normal.y < 0.7f)
        return hit;
    constexpr float kStep = 4.0f, kDrop = 8.0f;
    float best = reach + 1.0f;
    V3 bestAt = hit;
    constexpr float kHalf = 0.70710678f;
    static const V3 kDirections[8] = {{0.0f, 0.0f, 1.0f},    {kHalf, 0.0f, kHalf},   {1.0f, 0.0f, 0.0f},
                                      {kHalf, 0.0f, -kHalf}, {0.0f, 0.0f, -1.0f},    {-kHalf, 0.0f, -kHalf},
                                      {-1.0f, 0.0f, 0.0f},   {-kHalf, 0.0f, kHalf}};
    for (const V3& dir : kDirections) {
        for (float t = kStep; t <= reach; t += kStep) {
            V3 probe = hit + dir * t;
            float ground = 0.0f;
            bool have = groundAt({probe.x, hit.y + 20.0f, probe.z}, &ground);
            if (!have || ground < hit.y - kDrop) {
                float edge = t - kStep * 0.5f;
                if (edge < best) {
                    best = edge;
                    bestAt = hit + dir * edge;
                }
                break;
            }
        }
    }
    if (best > reach)
        return hit;
    *snapped = true;
    return {bestAt.x, hit.y, bestAt.z};
}

const std::vector<std::string>& GrindTerrainNames()
{
    // (scripts\game\terrain.qb's Terrain_Types, the TERRAIN_GRIND... entries in its order - the names only: the game
    // checks them against its own table before one is used, TerrainChecksums)
    static const std::vector<std::string> names = {
        "TERRAIN_GRINDTRAIN",        "TERRAIN_GRINDROPE",          "TERRAIN_GRINDWIRE",
        "TERRAIN_GRINDCONC",         "TERRAIN_GRINDROUNDMETALPOLE", "TERRAIN_GRINDCHAINLINK",
        "TERRAIN_GRINDMETAL",        "TERRAIN_GRINDWOODRAILING",   "TERRAIN_GRINDWOODLOG",
        "TERRAIN_GRINDWOOD",         "TERRAIN_GRINDPLASTIC",       "TERRAIN_GRINDELECTRICWIRE",
        "TERRAIN_GRINDCABLE",        "TERRAIN_GRINDCHAIN",         "TERRAIN_GRINDPLASTICBARRIER",
        "TERRAIN_GRINDNEONLIGHT",    "TERRAIN_GRINDGLASSMONSTER",  "TERRAIN_GRINDBANYONTREE",
        "TERRAIN_GRINDBRASSRAIL",    "TERRAIN_GRINDCATWALK",       "TERRAIN_GRINDTANKTURRET",
    };
    return names;
}

float FacingIntoWall(V3 normal)
{
    float length = std::sqrt(normal.x * normal.x + normal.z * normal.z);
    if (!(length > 1e-6f))
        return 0.0f;
    return std::atan2(-normal.x / length, -normal.z / length);
}

V3 FacingDirection(float facing) { return {std::sin(facing), 0.0f, std::cos(facing)}; }

bool LadderFoot(V3 hit, V3 normal, float out, const std::function<bool(V3 from, float* groundY)>& groundAt, V3* foot)
{
    if (!(std::fabs(normal.y) < 0.7f))
        return false;
    float length = std::sqrt(normal.x * normal.x + normal.z * normal.z);
    if (!(length > 1e-6f))
        return false;
    V3 at = {hit.x + normal.x / length * out, hit.y, hit.z + normal.z / length * out};
    float ground = 0.0f;
    if (groundAt({at.x, at.y + 4.0f, at.z}, &ground) && ground <= at.y + 4.0f && ground >= at.y - 64.0f)
        at.y = ground;
    *foot = at;
    return true;
}

bool LadderTop(V3 foot, V3 from, V3 dir, V3* top)
{
    // The closest points of the line foot + t (0, 1, 0) and the ray from + s dir.
    V3 w = foot - from;
    float b = dir.y, c = Dot(dir, dir), d = w.y, e = Dot(dir, w);
    float denominator = c - b * b;
    if (!(denominator > 1e-6f * c))
        return false; // (looking straight up or down the line)
    float t = (b * e - c * d) / denominator;
    if (!(t >= kLadderMin))
        return false;
    *top = {foot.x, foot.y + t, foot.z};
    return true;
}

bool SameSegment(V3 a, V3 b, V3 p, V3 q, float tolerance)
{
    auto near = [tolerance](V3 x, V3 y) {
        return std::fabs(x.x - y.x) <= tolerance && std::fabs(x.y - y.y) <= tolerance &&
               std::fabs(x.z - y.z) <= tolerance;
    };
    return (near(a, p) && near(b, q)) || (near(a, q) && near(b, p));
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kClass = 0x12B4E660;   // class
constexpr uint32_t kLinks = 0x2E7D5EE7;   // links
constexpr uint32_t kPos = 0x7F261953;     // pos
constexpr uint32_t kTerrainType = 0x54CF8532; // TerrainType

uint32_t Checksum(const std::string& text) { return Crc::GenerateCRCFromString(text.c_str()); }

bool InLevel() { return SkateScript::NodeManagerInstance() != nullptr && Scenery::MainScene() != nullptr; }

Script::CStruct* NodeStruct(uint32_t name)
{
    return name && SkateScript::NodeManagerInstance() ? SkateScript::GetNode(name, false) : nullptr;
}

// The level's rail manager (Mdl::Skate +0x24; null when there is none or its rails ride an object).
Obj::CRailManager* Rails()
{
    uint8_t* skate = GameVariable<uint8_t*>(0x007CE478);
    auto* manager = skate ? *reinterpret_cast<Obj::CRailManager**>(skate + 0x24) : nullptr;
    return manager && !manager->m_onObject ? manager : nullptr;
}

Obj::CRailNode* RailNodeNamed(uint32_t name)
{
    Obj::CRailManager* rails = Rails();
    if (!rails || !rails->m_nodes)
        return nullptr;
    for (int i = 0; i < rails->m_count; ++i) {
        if (rails->m_nodes[i].m_name == name)
            return &rails->m_nodes[i];
    }
    return nullptr;
}

// A structure's own links= (not an included structure's) as names.
std::vector<uint32_t> OwnLinks(Script::CStruct* node)
{
    std::vector<uint32_t> links;
    if (!node)
        return links;
    for (Script::CComponent* c = node->m_head; c; c = c->next) {
        if (c->name == kLinks && (c->Type() == Script::ESYMBOLTYPE_ARRAY || c->Type() == Script::ESYMBOLTYPE_ARRAYPOINTER) &&
            c->array) {
            links.clear();
            for (uint32_t i = 0; i < c->array->m_size; ++i)
                links.push_back(c->array->GetElement(i));
        }
    }
    return links;
}

// A level structure's own links= set (in place when it has one; removed for none).
void SetOwnLinks(Script::CStruct* node, const std::vector<uint32_t>& links)
{
    Script::CComponent* own = nullptr;
    for (Script::CComponent* c = node->m_head; c; c = c->next) {
        if (c->name == kLinks && (c->Type() == Script::ESYMBOLTYPE_ARRAY || c->Type() == Script::ESYMBOLTYPE_ARRAYPOINTER) &&
            c->array)
            own = c;
    }
    if (links.empty()) {
        if (own)
            node->RemoveComponent(kLinks);
        return;
    }
    if (own) {
        own->array->SetSizeAndType(static_cast<uint32_t>(links.size()), Script::ESYMBOLTYPE_NAME);
        for (size_t i = 0; i < links.size(); ++i)
            own->array->SetElement(static_cast<uint32_t>(i), links[i]);
        return;
    }
    Script::CArray* array = Script::NewArray();
    if (!array)
        return;
    array->Init();
    array->SetSizeAndType(static_cast<uint32_t>(links.size()), Script::ESYMBOLTYPE_NAME);
    for (size_t i = 0; i < links.size(); ++i)
        array->SetElement(static_cast<uint32_t>(i), links[i]);
    node->AddArrayPointer(kLinks, array);
}

// A structure's own pos= set (the one the game reads: the last).
void WritePos(Script::CStruct* node, V3 pos)
{
    Script::CComponent* own = nullptr;
    for (Script::CComponent* c = node->m_head; c; c = c->next) {
        if (c->name == kPos && c->Type() == Script::ESYMBOLTYPE_VECTOR && c->vector)
            own = c;
    }
    if (own) {
        own->vector->x = pos.x;
        own->vector->y = pos.y;
        own->vector->z = pos.z;
    } else {
        node->AddVector(kPos, pos.x, pos.y, pos.z);
    }
}

// A structure's own angles= set (as WritePos).
void WriteAngles(Script::CStruct* node, V3 angles)
{
    constexpr uint32_t kAngles = 0x9D2D0915; // angles
    Script::CComponent* own = nullptr;
    for (Script::CComponent* c = node->m_head; c; c = c->next) {
        if (c->name == kAngles && c->Type() == Script::ESYMBOLTYPE_VECTOR && c->vector)
            own = c;
    }
    if (own) {
        own->vector->x = angles.x;
        own->vector->y = angles.y;
        own->vector->z = angles.z;
    } else {
        node->AddVector(kAngles, angles.x, angles.y, angles.z);
    }
}

uint32_t ClassOf(Script::CStruct* node)
{
    uint32_t cls = 0;
    if (node)
        node->GetChecksum(kClass, &cls, 0);
    return cls;
}

uint32_t RailNodeClass() { static const uint32_t c = Checksum("RailNode"); return c; }
uint32_t ClimbingNodeClass() { static const uint32_t c = Checksum("ClimbingNode"); return c; }
uint32_t WaypointClass() { static const uint32_t c = Checksum("Waypoint"); return c; }
uint32_t PedestrianClass() { static const uint32_t c = Checksum("Pedestrian"); return c; }
uint32_t VehicleClass() { static const uint32_t c = Checksum("Vehicle"); return c; }

// The terrain checksums the game knows (Terrain_Types' checksum=): only those are looked up (0x004442C0 reads through
// a null entry for an unknown one).
bool KnownTerrain(uint32_t terrain)
{
    static const uint32_t kChecksumKey = Checksum("checksum");
    Script::CArray* types = Script::GetArray("Terrain_Types");
    if (!types || types->m_type != Script::ESYMBOLTYPE_STRUCTURE)
        return false;
    for (uint32_t i = 0; i < types->m_size; ++i) {
        auto* entry = reinterpret_cast<Script::CStruct*>(types->GetElement(i));
        uint32_t value = 0;
        if (entry && entry->GetChecksum(kChecksumKey, &value, 0) && value == terrain)
            return true;
    }
    return false;
}
bool TerrainByte(const std::string& name, uint8_t* value)
{
    uint32_t terrain = Checksum(name);
    if (!KnownTerrain(terrain))
        return false;
    *value = GameFunction<uint8_t(__cdecl*)(uint32_t)>(0x004442C0)(terrain);
    return true;
}

// What the editor has done to the level's own nodes: links= as the level had them (the structure, to know it's still
// the level loaded), the dropped points (and whether their rail node was on), terrains set (and the level's bytes).
struct OriginalLinks {
    Script::CStruct* node;
    std::vector<uint32_t> links;
};
std::map<uint32_t, OriginalLinks> g_originalLinks;
std::vector<uint32_t> g_dropped;
std::map<uint32_t, bool> g_dropActive;
struct TerrainSet {
    std::string terrain;
    uint8_t original;
};
std::map<uint32_t, TerrainSet> g_terrains;
// The rail manager's node arrays replaced by RebuildRails (freed with the level).
std::vector<void*> g_retired;
Obj::CRailNode* g_built; // (the node array RebuildRails made last)
// Objects to make again from their nodes (their path changed) once the game has removed them.
std::vector<uint32_t> g_remake;
// Drawing: the kind, the last point drawn (0: none yet). An armed pick: what for, and from which node.
Draw g_draw = Draw::None;
uint32_t g_drawLast;
Op g_pick = kOpCancel;
uint32_t g_pickFrom;
// Drawing a ladder: its foot (once the first click found a wall) and its heading.
bool g_ladderHaveFoot;
V3 g_ladderFoot;
float g_ladderFacing;
// The scenery piece the last snapped click hit (0: none) - a new rail's trick-object cluster.
uint32_t g_snapSector;
// Added points' fields that aren't in their nodes (by name): a rail's cluster, grindable on flat ground.
struct Extras {
    uint32_t cluster = 0;
    bool grindFlat = false;
    float speed = 0.0f; // (a speed-up's rate - ApplySpeeds)
};
constexpr uint32_t kTriggerScriptKey = 0x2CA8A299; // TriggerScript
// Level rail points given a speed-up (their rates).
std::map<uint32_t, float> g_levelSpeeds;
// The speed-ups in place: each node's structure, its own TriggerScript before ours (to put back) and the script that
// counted then (called first by ours); the per-point scripts last defined.
struct SpeedApplied {
    Script::CStruct* node = nullptr;
    Fields::Own original;
    uint32_t originalScript = 0;
};
std::map<uint32_t, SpeedApplied> g_speedApplied;
std::vector<uint8_t> g_speedDefined;
// The level's own speed-ups (a RailAccelerate in a rail point's TriggerScript): found once for each rail manager
// array (name, rate).
struct LevelSpeedCache {
    const Obj::CRailNode* nodes = nullptr;
    int count = -1;
    std::vector<std::pair<uint32_t, float>> found;
};
LevelSpeedCache g_levelSpeedCache;
std::unordered_map<uint32_t, Extras> g_extras;
// Level ladder points turned: their facing and the rail manager's orientation as the level had it.
struct FacingSet {
    float facing;
    Mth::Vector original;
};
std::map<uint32_t, FacingSet> g_facings;
// Level rail points marked grindable on flat ground.
std::vector<uint32_t> g_levelFlat;
// The trick objects the editor registered (name -> cluster).
std::map<uint32_t, uint32_t> g_trick;
// RailClear's override (FlatGrindable): the rail manager's nodes whose segment to the next is grindable on flat
// ground, found again when the nodes or the marks change.
struct FlatCache {
    const Obj::CRailNode* nodes = nullptr;
    int count = -1;
    unsigned generation = ~0u;
    std::vector<const Obj::CRailNode*> marked;
};
FlatCache g_flatCache;
unsigned g_flatGeneration;

std::vector<Point> AddedPoints()
{
    std::vector<Point> points;
    for (const NodeCreate::Spec& spec : NodeCreate::Current()) {
        if (!IsChainClass(spec.cls))
            continue;
        Point p;
        p.name = spec.name;
        p.cls = spec.cls;
        p.pos = spec.pos;
        p.like = spec.like;
        p.terrain = spec.terrain;
        p.links = spec.links;
        if (SameText(spec.cls, "ClimbingNode")) {
            p.facing = spec.angles.y; // (a ladder's Angles: (0, facing, 0))
            if (SameText(spec.type, "Ledge")) {
                p.type = "Ledge";
                for (const NodeCreate::Field& f : spec.extra) {
                    if (f.type == NodeCreate::Field::kFlag && SameText(f.text, "HangLeft"))
                        p.hang = kHangLeft;
                    if (f.type == NodeCreate::Field::kFlag && SameText(f.text, "HangRight"))
                        p.hang = kHangRight;
                }
            }
        }
        auto extras = g_extras.find(Checksum(spec.name));
        if (extras != g_extras.end()) {
            p.cluster = extras->second.cluster;
            p.grindFlat = extras->second.grindFlat;
            p.speed = extras->second.speed;
        }
        // (a rail point: where the rail manager has it - a sector moved with it takes it along)
        if (const Obj::CRailNode* node = RailNodeNamed(Checksum(spec.name)))
            p.pos = {node->m_pos.x, node->m_pos.y, node->m_pos.z};
        points.push_back(p);
    }
    return points;
}

NodeCreate::Spec SpecOf(const Point& p)
{
    NodeCreate::Spec spec;
    spec.name = p.name;
    spec.cls = p.cls;
    spec.pos = p.pos;
    spec.like = p.like;
    spec.terrain = p.terrain;
    spec.links = p.links;
    if (SameText(p.cls, "ClimbingNode")) {
        spec.type = "Ladder";
        spec.angles = {0.0f, p.facing, 0.0f};
        if (IsLedge(p)) {
            // (THUG2's own ledges: Type=Ledge, CreatedAtStart, a side to hang from when the level gives one)
            spec.type = "Ledge";
            if (p.hang) {
                NodeCreate::Field flag = {NodeCreate::Field::kFlag, ""};
                flag.text = p.hang == kHangLeft ? "HangLeft" : "HangRight";
                spec.extra.push_back(flag);
            }
        }
    }
    return spec;
}

// A climbing node's orientation (the rail manager's quaternion, as AddNode makes it from Angles) for a heading.
Mth::Vector ClimbOf(float facing)
{
    Mth::Matrix matrix;
    matrix.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    matrix.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    matrix.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    Mth::Vector angles = {0.0f, facing, 0.0f, 1.0f};
    Mth::Matrix* built =
        GameFunction<Mth::Matrix*(__thiscall*)(Mth::Matrix*, const Mth::Vector*)>(0x0041A730)(&matrix, &angles);
    Mth::Vector quat = {};
    GameFunction<void(__thiscall*)(Mth::Vector*, const Mth::Matrix*)>(0x0042E1F0)(&quat, built);
    return quat;
}

// The heading of a climbing node's orientation: its at row, flat.
float HeadingOfClimb(const Mth::Vector& climb)
{
    Mth::Matrix matrix;
    GameFunction<void(__thiscall*)(const Mth::Vector*, Mth::Matrix*)>(0x0042BC40)(&climb, &matrix);
    return std::atan2(matrix.row[2].x, matrix.row[2].z);
}

// A ladder point's heading now (the rail manager's; 0: not one).
float FacingNow(uint32_t name)
{
    const Obj::CRailNode* node = RailNodeNamed(name);
    return node && (node->m_flags & Obj::RAIL_FLAG_CLIMBING) ? HeadingOfClimb(node->m_climb) : 0.0f;
}

// A level ladder point's heading as the level has it.
float FacingOriginal(uint32_t name)
{
    auto it = g_facings.find(name);
    return it != g_facings.end() ? HeadingOfClimb(it->second.original) : FacingNow(name);
}

bool SameFacing(float a, float b) { return std::fabs(NodeEdit::WrapAngle(a - b)) < 1e-4f; }

std::vector<uint32_t> LevelLinksNow(uint32_t name) { return OwnLinks(NodeStruct(name)); }
std::vector<uint32_t> LevelLinksOriginal(uint32_t name)
{
    auto it = g_originalLinks.find(name);
    return it != g_originalLinks.end() ? it->second.links : LevelLinksNow(name);
}

void SetLevelLinks(uint32_t name, const std::vector<uint32_t>& links)
{
    Script::CStruct* node = NodeStruct(name);
    if (!node || NodeCreate::IsAdded(name))
        return;
    if (!g_originalLinks.count(name))
        g_originalLinks[name] = {node, OwnLinks(node)};
    if (OwnLinks(node) != links)
        SetOwnLinks(node, links);
}

// The rail manager node's box: to its next node, or its own point.
void Box(Obj::CRailNode* node)
{
    Mth::Vector here = node->m_pos;
    Mth::Vector there = node->m_next ? node->m_next->m_pos : node->m_pos;
    Obj::BoxOf(&here, &there, &node->m_min, &node->m_max);
}

void Remake(uint32_t name)
{
    auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(name));
    if (object && !(object->m_objectFlags & Obj::OBJECT_FLAG_DEAD))
        object->MarkAsDead();
    if (std::find(g_remake.begin(), g_remake.end(), name) == g_remake.end())
        g_remake.push_back(name);
    Log("level editor: %s's path changed - it is made again from its node", Objects::Label(name).c_str());
}

// The drawn rails' trick objects registered with the trick object manager (0x007CE500) as AddFromNodeArray registers
// the level's TrickObject rails (0x005896D0 the cluster, 0x00589080 the node's name mapped to it); those no longer
// wanted (or of another cluster now) taken out of its name map (+0x20, a Lst::HashTable).
void RegisterTrickObjects()
{
    uint8_t* manager = GameVariable<uint8_t*>(0x007CE500);
    if (!manager)
        return;
    std::map<uint32_t, uint32_t> want;
    for (const NodeCreate::Spec& spec : NodeCreate::Current()) {
        uint32_t name = Checksum(spec.name);
        auto extras = g_extras.find(name);
        if (SameText(spec.cls, "RailNode") && extras != g_extras.end() && extras->second.cluster && RailNodeNamed(name))
            want[name] = extras->second.cluster;
    }
    auto* objects = reinterpret_cast<Lst::HashTable*>(manager + 0x20);
    for (const auto& entry : g_trick) {
        auto it = want.find(entry.first);
        if (it == want.end() || it->second != entry.second)
            objects->FlushItem(entry.first);
    }
    for (const auto& entry : want) {
        auto it = g_trick.find(entry.first);
        if (it != g_trick.end() && it->second == entry.second)
            continue;
        auto* trickObjects = reinterpret_cast<Obj::CTrickObjectManager*>(manager);
        trickObjects->AddCluster(entry.second); // (false: there already)
        trickObjects->AddObjectToCluster(entry.first, entry.second);
    }
    if (DebugEnabled() && want != g_trick)
        Log("level editor: %u drawn rail points are trick objects", static_cast<unsigned>(want.size()));
    g_trick = want;
}

} // namespace

void RebuildRails()
{
    Obj::CRailManager* rails = Rails();
    if (!rails)
        return;
    Obj::CRailNode* old = rails->m_nodes;
    int oldCount = old ? rails->m_count : 0;
    // The nodes: the ones it has whose node is still there (a dropped one too - kept switched off, so the editor's
    // moves of it stay), then the overlay's new rail points.
    std::vector<int> kept;
    for (int i = 0; i < oldCount; ++i) {
        if (NodeStruct(old[i].m_name))
            kept.push_back(i);
    }
    std::vector<std::pair<uint32_t, Script::CStruct*>> added;
    for (const NodeCreate::Spec& spec : NodeCreate::Current()) {
        uint32_t name = Checksum(spec.name);
        Script::CStruct* node = NodeStruct(name);
        uint32_t cls = ClassOf(node);
        if (!node || (cls != RailNodeClass() && cls != ClimbingNodeClass()))
            continue;
        bool there = false;
        for (int i : kept)
            there = there || old[i].m_name == name;
        if (!there)
            added.push_back({name, node});
    }
    int count = static_cast<int>(kept.size() + added.size());
    Obj::CRailNode* nodes = nullptr;
    if (count) {
        Mem::Manager* manager = Mem::Manager::Get();
        manager->PushContext(manager->m_bottomUpHeap);
        nodes = static_cast<Obj::CRailNode*>(Mem::Malloc(count * sizeof(Obj::CRailNode)));
        Mem::Manager::Get()->PopContext();
        if (!nodes) {
            Log("level editor: no memory for the rail manager's nodes - rails not rebuilt");
            return;
        }
    }
    for (size_t i = 0; i < kept.size(); ++i)
        nodes[i] = old[kept[i]];
    rails->m_nodes = nodes;
    rails->m_count = count;
    for (size_t i = 0; i < added.size(); ++i) {
        rails->m_adding = static_cast<int>(kept.size() + i);
        rails->AddNode(added[i].first, added[i].second); // (the game's own: flags, place, terrain)
    }
    rails->m_adding = count;
    // On / off: dropped points off (their state noted first), shown ones back as they were.
    std::unordered_map<uint32_t, int> index;
    for (int i = 0; i < count; ++i) {
        Obj::CRailNode* node = &nodes[i];
        index.emplace(node->m_name, i);
        node->m_next = node->m_prev = nullptr;
        bool dropped = std::find(g_dropped.begin(), g_dropped.end(), node->m_name) != g_dropped.end();
        auto was = g_dropActive.find(node->m_name);
        if (dropped) {
            if (was == g_dropActive.end())
                g_dropActive[node->m_name] = (node->m_flags & Obj::RAIL_FLAG_ACTIVE) != 0;
            node->m_flags &= ~Obj::RAIL_FLAG_ACTIVE;
        } else if (was != g_dropActive.end()) {
            node->m_flags = was->second ? node->m_flags | Obj::RAIL_FLAG_ACTIVE : node->m_flags & ~Obj::RAIL_FLAG_ACTIVE;
            g_dropActive.erase(was);
        }
    }
    // The links, as AddFromNodeArray makes them: a node's next is its first link (or a DefaultLine one), the linked
    // node's previous likewise. Dropped points take no part.
    auto isDropped = [&](uint32_t name) { return std::find(g_dropped.begin(), g_dropped.end(), name) != g_dropped.end(); };
    for (int i = 0; i < count; ++i) {
        Obj::CRailNode* node = &nodes[i];
        if (isDropped(node->m_name))
            continue;
        for (uint32_t link : OwnLinks(NodeStruct(node->m_name))) {
            auto it = index.find(link);
            if (it == index.end() || isDropped(link))
                continue;
            Obj::CRailNode* other = &nodes[it->second];
            if (!node->m_next || (other->m_flags & Obj::RAIL_FLAG_DEFAULT_LINE))
                node->m_next = other;
            if (!other->m_prev || (node->m_flags & Obj::RAIL_FLAG_DEFAULT_LINE))
                other->m_prev = node;
        }
    }
    for (int i = 0; i < count; ++i)
        Box(&nodes[i]);
    // An added point's terrain from its node (it is made again when its terrain changes, but its rail manager node was
    // kept); terrains the editor set on the level's points (copied nodes keep theirs).
    for (size_t i = 0; i < kept.size(); ++i) {
        Obj::CRailNode* node = &nodes[i];
        uint32_t terrain = 0;
        Script::CStruct* params = NodeCreate::IsAdded(node->m_name) ? NodeStruct(node->m_name) : nullptr;
        if (params && !(node->m_flags & Obj::RAIL_FLAG_CLIMBING) && params->GetChecksum(kTerrainType, &terrain, 0) &&
            KnownTerrain(terrain))
            node->m_terrain = GameFunction<uint8_t(__cdecl*)(uint32_t)>(0x004442C0)(terrain);
    }
    for (const auto& entry : g_terrains) {
        auto it = index.find(entry.first);
        uint8_t value = 0;
        if (it != index.end() && TerrainByte(entry.second.terrain, &value))
            nodes[it->second].m_terrain = value;
    }
    // Ladders: an added point's orientation from its node's Angles (turned since it was made), a level point's as the
    // editor turned it.
    for (int i = 0; i < count; ++i) {
        Obj::CRailNode* node = &nodes[i];
        if (!(node->m_flags & Obj::RAIL_FLAG_CLIMBING))
            continue;
        if (NodeCreate::IsAdded(node->m_name)) {
            Mth::Vector angles;
            SkateScript::GetAngles(NodeStruct(node->m_name), &angles);
            node->m_climb = ClimbOf(angles.y);
        } else if (g_facings.count(node->m_name)) {
            node->m_climb = ClimbOf(g_facings[node->m_name].facing);
        }
    }
    if (old)
        g_retired.push_back(old); // (a skater's grind may point into it: freed with the level)
    g_built = nodes;
    RegisterTrickObjects();
    ++g_flatGeneration;
    Scenery::RailsRebuilt();
    Nodes::Invalidate();
    if (DebugEnabled())
        Log("level editor: rail manager built again - %d nodes (%u kept, %u new)", count,
            static_cast<unsigned>(kept.size()), static_cast<unsigned>(added.size()));
}

State Current()
{
    State state;
    if (!InLevel())
        return state;
    state.points = AddedPoints();
    for (const auto& entry : g_originalLinks) {
        Script::CStruct* node = NodeStruct(entry.first);
        if (node != entry.second.node)
            continue;
        std::vector<uint32_t> now = OwnLinks(node);
        if (now != entry.second.links)
            state.links.push_back({entry.first, now});
    }
    state.dropped = g_dropped;
    std::sort(state.dropped.begin(), state.dropped.end());
    for (const auto& entry : g_terrains)
        state.terrains.push_back({entry.first, entry.second.terrain});
    for (const auto& entry : g_facings)
        state.facings.push_back({entry.first, entry.second.facing});
    state.grindFlat = g_levelFlat;
    std::sort(state.grindFlat.begin(), state.grindFlat.end());
    for (const auto& entry : g_levelSpeeds)
        state.speeds.push_back({entry.first, entry.second}); // (a map: sorted by name)
    return state;
}

bool SetState(const State& target)
{
    if (!InLevel())
        return false;
    State before = Current();
    if (SameState(before, target))
        return true; // (nothing to do: the rail manager isn't built again)
    // Points: those not wanted (or wanted different, other than place and links) removed; the missing made (one
    // re-registration); places and links set.
    std::vector<Point> now = before.points;
    for (const Point& p : now) {
        const Point* want = nullptr;
        for (const Point& t : target.points)
            want = SameText(t.name, p.name.c_str()) ? &t : want;
        if (!want || want->cls != p.cls || want->like != p.like || want->terrain != p.terrain ||
            !SameText(want->type, p.type.c_str()) || want->hang != p.hang) {
            NodeCreate::Spec removed;
            int at;
            NodeCreate::Remove(Checksum(p.name), &removed, &at);
        }
    }
    std::vector<NodeCreate::Spec> make;
    std::vector<Point> remaining = AddedPoints();
    for (const Point& t : target.points) {
        bool there = false;
        for (const Point& p : remaining)
            there = there || SameText(t.name, p.name.c_str());
        if (!there)
            make.push_back(SpecOf(t));
    }
    if (!make.empty()) {
        std::string message;
        NodeCreate::CreateAll(make, &message);
        if (!message.empty())
            Log("level editor: %s", message.c_str());
    }
    for (const Point& t : target.points) {
        uint32_t name = Checksum(t.name);
        NodeCreate::SetLinks(name, t.links);
        if (Script::CStruct* node = NodeStruct(name)) {
            Mth::Vector pos;
            SkateScript::GetPosition(node, &pos);
            if (pos.x != t.pos.x || pos.y != t.pos.y || pos.z != t.pos.z)
                WritePos(node, t.pos);
            Mth::Vector angles;
            SkateScript::GetAngles(node, &angles);
            if (SameText(t.cls, "ClimbingNode") && (angles.x != 0.0f || angles.y != t.facing || angles.z != 0.0f))
                WriteAngles(node, {0.0f, t.facing, 0.0f}); // (a ladder turned: RebuildRails turns its rail node)
        }
    }
    g_extras.clear();
    for (const Point& t : target.points) {
        if (t.cluster || t.grindFlat || t.speed > 0.0f)
            g_extras[Checksum(t.name)] = {t.cluster, t.grindFlat, t.speed > 0.0f ? ClampSpeed(t.speed) : 0.0f};
    }
    g_levelSpeeds.clear();
    for (const SpeedEdit& e : target.speeds) {
        if (!NodeCreate::IsAdded(e.name) && NodeStruct(e.name))
            g_levelSpeeds[e.name] = ClampSpeed(e.rate);
    }
    // Level ladders turned (the rail manager's orientation as the level had it kept to put back), level rails marked
    // grindable on flat ground.
    for (auto it = g_facings.begin(); it != g_facings.end();) {
        bool wanted = false;
        for (const FacingEdit& f : target.facings)
            wanted = wanted || f.name == it->first;
        if (!wanted) {
            if (Obj::CRailNode* node = RailNodeNamed(it->first))
                node->m_climb = it->second.original;
            it = g_facings.erase(it);
        } else {
            ++it;
        }
    }
    for (const FacingEdit& f : target.facings) {
        Obj::CRailNode* node = RailNodeNamed(f.name);
        if (!node || !(node->m_flags & Obj::RAIL_FLAG_CLIMBING) || NodeCreate::IsAdded(f.name))
            continue;
        if (!g_facings.count(f.name))
            g_facings[f.name] = {f.facing, node->m_climb};
        g_facings[f.name].facing = f.facing;
    }
    g_levelFlat.clear();
    for (uint32_t name : target.grindFlat) {
        if (!NodeCreate::IsAdded(name) && NodeStruct(name))
            g_levelFlat.push_back(name);
    }
    // The level's links: as the target has them, else as the level had them.
    std::set<uint32_t> names;
    for (const auto& entry : g_originalLinks)
        names.insert(entry.first);
    for (const LinkEdit& l : target.links)
        names.insert(l.name);
    std::vector<uint32_t> pathsChanged;
    for (uint32_t name : names) {
        std::vector<uint32_t> want = LevelLinksOriginal(name);
        for (const LinkEdit& l : target.links)
            if (l.name == name)
                want = l.links;
        if (LevelLinksNow(name) != want) {
            uint32_t cls = ClassOf(NodeStruct(name));
            if (cls == PedestrianClass() || cls == VehicleClass())
                pathsChanged.push_back(name);
        }
        SetLevelLinks(name, want);
    }
    // Dropped points: hidden with phase 3's marker (shown again when no longer dropped).
    std::vector<uint32_t> dropped = target.dropped;
    for (uint32_t name : std::vector<uint32_t>(g_dropped)) {
        if (std::find(dropped.begin(), dropped.end(), name) == dropped.end())
            NodeCreate::Hide(name, false);
    }
    std::vector<uint32_t> kept;
    for (uint32_t name : dropped) {
        if (NodeCreate::IsAdded(name) || !NodeStruct(name))
            continue;
        if (NodeCreate::Hidden(name) || NodeCreate::Hide(name, true))
            kept.push_back(name);
    }
    g_dropped = kept;
    // Terrains (level points): put back where no longer set.
    for (auto it = g_terrains.begin(); it != g_terrains.end();) {
        bool wanted = false;
        for (const TerrainEdit& t : target.terrains)
            wanted = wanted || t.name == it->first;
        if (!wanted) {
            if (Obj::CRailNode* node = RailNodeNamed(it->first))
                node->m_terrain = it->second.original;
            it = g_terrains.erase(it);
        } else {
            ++it;
        }
    }
    for (const TerrainEdit& t : target.terrains) {
        Obj::CRailNode* node = RailNodeNamed(t.name);
        uint8_t value = 0;
        if (!node || !TerrainByte(t.terrain, &value))
            continue;
        if (!g_terrains.count(t.name))
            g_terrains[t.name] = {t.terrain, node->m_terrain};
        g_terrains[t.name].terrain = t.terrain;
    }
    RebuildRails();
    // The added points' places in the rail manager (a point made before keeps its node there).
    for (const Point& t : target.points)
        MoveAddedPoint(Checksum(t.name), t.pos);
    for (uint32_t name : pathsChanged)
        Remake(name);
    ApplySpeeds();
    Nodes::Invalidate();
    return true;
}

void ForgetLevel()
{
    for (const auto& entry : g_originalLinks) {
        Script::CStruct* node = NodeStruct(entry.first);
        if (node && node == entry.second.node)
            SetOwnLinks(node, entry.second.links);
    }
    g_originalLinks.clear();
    g_dropped.clear();
    g_dropActive.clear();
    g_terrains.clear();
    g_remake.clear();
    g_draw = Draw::None;
    g_drawLast = 0;
    g_pick = kOpCancel;
    g_ladderHaveFoot = false;
    // (the rail manager and the trick object manager are the level's: they go with it)
    g_extras.clear();
    g_facings.clear();
    g_levelFlat.clear();
    g_trick.clear();
    ++g_flatGeneration;
    // (the speed-ups' TriggerScripts: the level's nodes put back while its array is the one loaded)
    for (const auto& entry : g_speedApplied) {
        Script::CStruct* node = NodeStruct(entry.first);
        if (node && node == entry.second.node)
            Fields::PutIn(node, kTriggerScriptKey, false, entry.second.original);
    }
    g_speedApplied.clear();
    g_levelSpeeds.clear();
    g_levelSpeedCache = LevelSpeedCache{};
}

namespace {

// The rail manager's nodes whose segment to the next is grindable on flat ground: an added point marked so, a level
// point marked so, or a node carrying the flag ed_grind_flat (a level baked by apply_layout.ts).
void RefreshFlat(Obj::CRailManager* rails)
{
    if (g_flatCache.nodes == rails->m_nodes && g_flatCache.count == rails->m_count &&
        g_flatCache.generation == g_flatGeneration)
        return;
    g_flatCache.nodes = rails->m_nodes;
    g_flatCache.count = rails->m_count;
    g_flatCache.generation = g_flatGeneration;
    g_flatCache.marked.clear();
    if (!rails->m_nodes || !SkateScript::NodeManagerInstance())
        return;
    static const uint32_t kFlag = Checksum("ed_grind_flat");
    for (int i = 0; i < rails->m_count; ++i) {
        const Obj::CRailNode* node = &rails->m_nodes[i];
        if ((node->m_flags & Obj::RAIL_FLAG_CLIMBING) || !node->m_next)
            continue;
        auto extras = g_extras.find(node->m_name);
        bool marked = (extras != g_extras.end() && extras->second.grindFlat) ||
                      std::find(g_levelFlat.begin(), g_levelFlat.end(), node->m_name) != g_levelFlat.end();
        if (!marked) {
            Script::CStruct* params = NodeStruct(node->m_name);
            marked = params && params->ContainsFlag(kFlag);
        }
        if (marked)
            g_flatCache.marked.push_back(node);
    }
    if (DebugEnabled() && !g_flatCache.marked.empty())
        Log("level editor: %u rail segments grindable on flat ground", static_cast<unsigned>(g_flatCache.marked.size()));
}

bool FlatHook(const Mth::Vector* a, const Mth::Vector* b) { return FlatGrindable({a->x, a->y, a->z}, {b->x, b->y, b->z}); }

} // namespace

bool IsLedgeNode(uint32_t name)
{
    static const uint32_t kLedge = Checksum("Ledge");
    int index = Nodes::Find(name);
    return index >= 0 && Nodes::All()[index].cls == ClimbingNodeClass() && Nodes::All()[index].type == kLedge;
}

void ApplySpeeds()
{
    if (!SkateScript::NodeManagerInstance() || (g_speedApplied.empty() && g_extras.empty() && g_levelSpeeds.empty()))
        return;
    // What's wanted (none in a network session: the edited level's changes are offline only).
    std::map<uint32_t, float> want;
    if (!Layout::Refused()) {
        for (const auto& entry : g_extras) {
            if (entry.second.speed > 0.0f)
                want[entry.first] = entry.second.speed;
        }
        for (const auto& entry : g_levelSpeeds)
            want[entry.first] = entry.second;
    }
    // Those no longer wanted put back; a node whose structure was made again (the overlay registered again) forgotten
    // (its new structure has no TriggerScript of ours).
    for (auto it = g_speedApplied.begin(); it != g_speedApplied.end();) {
        Script::CStruct* node = NodeStruct(it->first);
        if (node != it->second.node) {
            it = g_speedApplied.erase(it);
            continue;
        }
        if (!want.count(it->first)) {
            Fields::PutIn(node, kTriggerScriptKey, false, it->second.original);
            it = g_speedApplied.erase(it);
            continue;
        }
        ++it;
    }
    std::vector<SpeedScript> scripts;
    for (const auto& w : want) {
        Script::CStruct* node = NodeStruct(w.first);
        if (!node)
            continue;
        uint32_t ours = Checksum(SpeedScriptName(w.first));
        auto it = g_speedApplied.find(w.first);
        if (it == g_speedApplied.end()) {
            SpeedApplied a;
            a.node = node;
            a.original = Fields::OwnIn(node, kTriggerScriptKey, false);
            node->GetChecksum(kTriggerScriptKey, &a.originalScript, 0);
            if (a.originalScript == ours)
                a.originalScript = 0;
            it = g_speedApplied.emplace(w.first, a).first;
        }
        scripts.push_back({w.first, it->second.originalScript, w.second});
    }
    if (scripts.empty())
        return;
    // The scripts: the fixed two (once - they stay defined), the points' when they change.
    static const uint32_t kRailSpeed = Checksum("ed_RailSpeed");
    Script::CSymbolTableEntry* fixed = Script::LookUpSymbol(kRailSpeed);
    if (!fixed || fixed->type != Script::ESYMBOLTYPE_QSCRIPT)
        Script::LoadQBData("editor\\thug2_decomp_railspeed.qb", RailSpeedQb().data(), 0, 0, 0);
    std::vector<uint8_t> file = SpeedQbFile(scripts);
    if (file != g_speedDefined) {
        Script::LoadQBData("editor\\thug2_decomp_railspeeds.qb", file.data(), 0, 0, 0);
        g_speedDefined = file;
        if (DebugEnabled())
            Log("level editor: %u rail speed-up scripts defined", static_cast<unsigned>(scripts.size()));
    }
    // Each node's TriggerScript ours (its own component, after any shared one: it counts).
    for (const SpeedScript& s : scripts) {
        Script::CStruct* node = NodeStruct(s.node);
        uint32_t ours = Checksum(SpeedScriptName(s.node)), now = 0;
        if (!node || (node->GetChecksum(kTriggerScriptKey, &now, 0) && now == ours))
            continue;
        Fields::Own own;
        own.present = true;
        own.value.kind = Fields::Kind::Name;
        own.value.checksum = ours;
        own.index = -1;
        Fields::PutIn(node, kTriggerScriptKey, false, own);
    }
}

std::vector<SpeedSegment> SpeedSegments(V3 near, float radius)
{
    std::vector<SpeedSegment> out;
    Obj::CRailManager* rails = Rails();
    if (!rails || !rails->m_nodes || !SkateScript::NodeManagerInstance())
        return out;
    // (the level's own RailAccelerate points: found once for each rail manager array)
    if (g_levelSpeedCache.nodes != rails->m_nodes || g_levelSpeedCache.count != rails->m_count) {
        g_levelSpeedCache.nodes = rails->m_nodes;
        g_levelSpeedCache.count = rails->m_count;
        g_levelSpeedCache.found.clear();
        for (int i = 0; i < rails->m_count; ++i) {
            const Obj::CRailNode* node = &rails->m_nodes[i];
            Script::CStruct* params = node->m_next ? NodeStruct(node->m_name) : nullptr;
            uint32_t script = 0;
            if (!params || !params->GetChecksum(kTriggerScriptKey, &script, 0) || !script)
                continue;
            auto applied = g_speedApplied.find(node->m_name);
            if (applied != g_speedApplied.end())
                script = applied->second.originalScript; // (the level's own script, under ours)
            Triggers::Calls calls = Triggers::ScriptCalls(script);
            if (calls.railSpeed)
                g_levelSpeedCache.found.push_back({node->m_name, calls.railRate});
        }
    }
    float r2 = radius * radius;
    auto add = [&](const Obj::CRailNode* node, float rate, bool level) {
        if (!node || !node->m_next || (node->m_flags & Obj::RAIL_FLAG_CLIMBING))
            return;
        V3 a = {node->m_pos.x, node->m_pos.y, node->m_pos.z};
        V3 b = {node->m_next->m_pos.x, node->m_next->m_pos.y, node->m_next->m_pos.z};
        V3 da = a - near, db = b - near;
        if (Dot(da, da) > r2 && Dot(db, db) > r2)
            return;
        out.push_back({a, b, rate, level});
    };
    for (const auto& entry : g_speedApplied) {
        auto extras = g_extras.find(entry.first);
        auto level = g_levelSpeeds.find(entry.first);
        float rate = extras != g_extras.end() ? extras->second.speed : level != g_levelSpeeds.end() ? level->second : 0.0f;
        if (rate > 0.0f)
            add(RailNodeNamed(entry.first), rate, false);
    }
    for (const auto& found : g_levelSpeedCache.found) {
        if (!g_speedApplied.count(found.first))
            add(RailNodeNamed(found.first), found.second, true);
    }
    return out;
}

bool FlatGrindable(V3 a, V3 b)
{
    Obj::CRailManager* rails = Rails();
    if (!rails)
        return false;
    RefreshFlat(rails);
    for (const Obj::CRailNode* node : g_flatCache.marked) {
        const Obj::CRailNode* next = node->m_next;
        if (next && SameSegment(a, b, {node->m_pos.x, node->m_pos.y, node->m_pos.z},
                                {next->m_pos.x, next->m_pos.y, next->m_pos.z}, 0.01f))
            return true;
    }
    return false;
}

void Tick()
{
    static bool hooked;
    if (!hooked) {
        Obj::SetRailClearOverride(&FlatHook); // (RailClear asks it - see the top)
        hooked = true;
    }
    // The rail manager's old node arrays: freed once the level's rails are no longer the ones built here (the game
    // made a new level's - its Cleanup freed ours - or there are none).
    if (!g_retired.empty()) {
        Obj::CRailManager* rails = Rails();
        if (!rails || rails->m_nodes != g_built) {
            for (void* nodes : g_retired)
                Mem::FreeMalloc(nodes);
            g_retired.clear();
            g_built = nullptr;
        }
    }
    ApplySpeeds();
    if (g_remake.empty())
        return;
    if (!InLevel()) {
        g_remake.clear();
        return;
    }
    for (auto it = g_remake.begin(); it != g_remake.end();) {
        if (Obj::FindObject(*it)) {
            ++it; // (not removed yet: the game deletes dead objects in its update)
            continue;
        }
        if (Script::CStruct* node = NodeStruct(*it))
            GameFunction<int(__cdecl*)(Script::CStruct*)>(0x005A2E50)(node);
        Log("level editor: %s made again (its path from its links)", Objects::Label(*it).c_str());
        it = g_remake.erase(it);
    }
}

bool IsChainNode(uint32_t name)
{
    int index = Nodes::Find(name);
    if (index < 0)
        return false;
    Nodes::Family family = Nodes::All()[index].family;
    return family == Nodes::Family::Rail || family == Nodes::Family::Ladder || family == Nodes::Family::Path;
}
bool IsPath(uint32_t name)
{
    int index = Nodes::Find(name);
    return index >= 0 && Nodes::All()[index].family == Nodes::Family::Path;
}
bool IsLadder(uint32_t name)
{
    int index = Nodes::Find(name);
    return index >= 0 && Nodes::All()[index].family == Nodes::Family::Ladder;
}

bool IsAddedPoint(uint32_t name)
{
    if (!NodeCreate::IsAdded(name))
        return false;
    int index = Nodes::Find(name);
    return index >= 0 && (Nodes::All()[index].family == Nodes::Family::Rail ||
                          Nodes::All()[index].family == Nodes::Family::Ladder ||
                          Nodes::All()[index].family == Nodes::Family::Path);
}

bool MoveAddedPoint(uint32_t name, V3 pos)
{
    Script::CStruct* node = NodeCreate::IsAdded(name) ? NodeStruct(name) : nullptr;
    if (!node)
        return false;
    Mth::Vector now;
    SkateScript::GetPosition(node, &now);
    if (now.x != pos.x || now.y != pos.y || now.z != pos.z)
        WritePos(node, pos);
    if (Obj::CRailNode* rail = RailNodeNamed(name)) {
        if (rail->m_pos.x != pos.x || rail->m_pos.y != pos.y || rail->m_pos.z != pos.z) {
            rail->m_pos = {pos.x, pos.y, pos.z, 1.0f};
            Obj::CRailManager* rails = Rails();
            for (int i = 0; i < rails->m_count; ++i) {
                if (&rails->m_nodes[i] == rail || rails->m_nodes[i].m_next == rail)
                    Box(&rails->m_nodes[i]);
            }
            Scenery::RailsRebuilt(); // (the node layer reads the places again)
        }
    }
    int index = Nodes::Find(name);
    if (index >= 0)
        Nodes::Reread(index);
    return true;
}

namespace {

// The chain graph of a family's nodes (as their links= are now): rails (RailNode), paths (Waypoint).
Graph GraphOf(Nodes::Family family)
{
    Graph graph;
    const std::vector<Nodes::Node>& nodes = Nodes::All();
    for (const Nodes::Node& node : nodes) {
        if (node.family != family || !node.name || node.hidden)
            continue;
        std::vector<uint32_t> links;
        for (int link : node.links) {
            if (link >= 0 && link < static_cast<int>(nodes.size()) && nodes[link].family == family && !nodes[link].hidden)
                links.push_back(nodes[link].name);
        }
        graph[node.name] = links;
    }
    return graph;
}

Nodes::Family FamilyOfName(uint32_t name)
{
    int index = Nodes::Find(name);
    return index >= 0 ? Nodes::All()[index].family : Nodes::Family::Other;
}

V3 PlaceOf(uint32_t name)
{
    NodeEdit::Values values;
    if (NodeEdit::Read(name, &values))
        return values.pos;
    int index = Nodes::Find(name);
    return index >= 0 ? Nodes::All()[index].pos : V3{};
}

// A level node's links in the state: an edit, or none when they're as the level had them.
void SetLinkEdit(State* state, uint32_t name, const std::vector<uint32_t>& links)
{
    auto it = std::find_if(state->links.begin(), state->links.end(), [&](const LinkEdit& l) { return l.name == name; });
    if (links == LevelLinksOriginal(name)) {
        if (it != state->links.end())
            state->links.erase(it);
    } else if (it != state->links.end()) {
        it->links = links;
    } else {
        state->links.push_back({name, links});
        SortByName(&state->links);
    }
}

// The graph's links put into the state: an added point's on its point, a level node's as a links edit (none when as
// the level had them).
void PutGraph(State* state, const Graph& graph)
{
    for (const auto& entry : graph) {
        bool point = false;
        for (Point& p : state->points) {
            if (Checksum(p.name) == entry.first) {
                p.links = entry.second;
                point = true;
            }
        }
        if (point)
            continue;
        // (a level node: only the links within the family are in the graph - others it has are kept)
        std::vector<uint32_t> links = entry.second;
        Nodes::Family family = FamilyOfName(entry.first);
        for (uint32_t other : LevelLinksNow(entry.first)) {
            if (FamilyOfName(other) != family && std::find(links.begin(), links.end(), other) == links.end())
                links.push_back(other);
        }
        SetLinkEdit(state, entry.first, links);
    }
}

// A new point's name: ed_rail01 / ed_path01... free in the level and in the state.
std::string FreshPointName(Nodes::Family family, const State& state, bool ledge = false)
{
    const char* stem = family == Nodes::Family::Path     ? "ed_path"
                       : ledge                           ? "ed_ledge"
                       : family == Nodes::Family::Ladder ? "ed_ladder"
                                                         : "ed_rail";
    return NodeCreate::FreshName(stem, [&](const std::string& name) {
        for (const Point& p : state.points) {
            if (SameText(p.name, name.c_str()))
                return true;
        }
        return NodeCreate::NameTaken(name);
    });
}

// A new point drawn on from `from` (0: a new chain of `family`): its class, and the fields it copies.
Point NewPoint(Nodes::Family family, uint32_t from, V3 pos, const State& state)
{
    // (a ledge: drawn as one, or drawn on from one)
    bool ledge = family == Nodes::Family::Ladder && (g_draw == Draw::Ledge || (from && IsLedgeNode(from)));
    Point p;
    p.name = FreshPointName(family, state, ledge);
    p.cls = family == Nodes::Family::Path ? "Waypoint" : family == Nodes::Family::Ladder ? "ClimbingNode" : "RailNode";
    p.pos = pos;
    if (ledge)
        p.type = "Ledge";
    if (from) {
        p.like = from;
        bool added = false;
        for (const Point& q : state.points) {
            if (Checksum(q.name) == from) {
                // (an added point: its own template and terrain - the layout makes them in one go - its cluster and
                // whether it's grindable on flat ground; a ledge's hang side)
                p.like = q.like;
                p.terrain = q.terrain;
                p.cluster = q.cluster;
                p.grindFlat = q.grindFlat;
                p.hang = q.hang;
                added = true;
            }
        }
        if (!added && family == Nodes::Family::Rail) {
            // (a level rail's point: its cluster when it's a trick object, as the level registers it)
            Script::CStruct* node = NodeStruct(from);
            static const uint32_t kTrickObject = Checksum("TrickObject"), kCluster = Checksum("Cluster");
            if (node && node->ContainsComponentNamed(kTrickObject)) {
                p.cluster = from;
                node->GetChecksum(kCluster, &p.cluster, 0);
            }
            p.grindFlat = std::find(state.grindFlat.begin(), state.grindFlat.end(), from) != state.grindFlat.end();
        }
    } else if (family == Nodes::Family::Rail) {
        // (a new rail: a trick object of the scenery piece the click hit, as the levels name their rails' clusters -
        // else of its own)
        p.cluster = g_snapSector ? g_snapSector : Checksum(p.name);
    }
    return p;
}

bool Apply(const State& state, std::string* message, const char* done)
{
    if (!SetState(state)) {
        *message = "Not in a level";
        return false;
    }
    *message = done;
    return true;
}

// A ladder point's heading set in the state: an added point's facing, a level point's `facing` edit (none when as the
// level has it).
void SetFacing(State* state, uint32_t name, float facing)
{
    for (Point& p : state->points) {
        if (Checksum(p.name) == name) {
            p.facing = facing;
            return;
        }
    }
    auto it = std::find_if(state->facings.begin(), state->facings.end(),
                           [&](const FacingEdit& f) { return f.name == name; });
    if (SameFacing(facing, FacingOriginal(name))) {
        if (it != state->facings.end())
            state->facings.erase(it);
    } else if (it != state->facings.end()) {
        it->facing = facing;
    } else {
        state->facings.push_back({name, facing});
        SortByName(&state->facings);
    }
}

// The operations on a ladder (its two points - or a level's climbing chain - together).
bool LadderOp(Op op, uint32_t name, const Nodes::Node& node, uint32_t arg, std::string* message, uint32_t* select)
{
    if (node.hidden) {
        *message = "Deleted";
        return false;
    }
    State state = Current();
    std::vector<uint32_t> points = ChainOf(name);
    if (node.added) {
        // (an added ladder: its points by their links too - the rail manager's chain may not have them yet)
        Graph graph;
        for (const Point& p : state.points) {
            if (SameText(p.cls, "ClimbingNode"))
                graph[Checksum(p.name)] = p.links;
        }
        for (uint32_t point : Connected(graph, name)) {
            if (std::find(points.begin(), points.end(), point) == points.end())
                points.push_back(point);
        }
    }
    switch (op) {
    case kOpDelete: {
        bool level = false;
        for (uint32_t point : points) {
            auto added = std::find_if(state.points.begin(), state.points.end(),
                                      [&](const Point& p) { return Checksum(p.name) == point; });
            if (added != state.points.end()) {
                state.points.erase(added);
                continue;
            }
            level = true;
            if (std::find(state.dropped.begin(), state.dropped.end(), point) == state.dropped.end())
                state.dropped.push_back(point);
            state.facings.erase(std::remove_if(state.facings.begin(), state.facings.end(),
                                               [&](const FacingEdit& f) { return f.name == point; }),
                                state.facings.end());
        }
        std::sort(state.dropped.begin(), state.dropped.end());
        return Apply(state, message, level ? "Ladder hidden" : "Ladder deleted");
    }
    case kOpTurn: {
        int notches = static_cast<int>(arg);
        float facing = NodeEdit::WrapAngle(FacingNow(name) + static_cast<float>(notches) * (15.0f * 3.14159265f / 180.0f));
        for (uint32_t point : points)
            SetFacing(&state, point, facing);
        *select = name;
        char text[96];
        std::snprintf(text, sizeof(text), "Ladder facing %.0f degrees",
                      static_cast<double>(NodeEdit::HeadingDegrees({0.0f, facing, 0.0f})));
        std::string done = text;
        return Apply(state, message, done.c_str());
    }
    case kOpFacingHome: {
        bool any = false;
        for (uint32_t point : points) {
            auto it = std::find_if(state.facings.begin(), state.facings.end(),
                                   [&](const FacingEdit& f) { return f.name == point; });
            if (it != state.facings.end()) {
                state.facings.erase(it);
                any = true;
            }
        }
        if (!any)
            return false; // (nothing to put back - no message: Home's own says what it did)
        *select = name;
        return Apply(state, message, "Ladder facing as the level has it");
    }
    default:
        *message = "Ladder selected";
        return false;
    }
}

} // namespace

std::vector<uint32_t> ChainOf(uint32_t name)
{
    Nodes::Family family = FamilyOfName(name);
    if (family == Nodes::Family::Rail || family == Nodes::Family::Ladder) {
        std::vector<uint32_t> rail = Scenery::RailOf(name);
        return rail.empty() ? std::vector<uint32_t>{name} : rail;
    }
    if (family == Nodes::Family::Path)
        return Connected(GraphOf(family), name);
    return {name};
}

bool Do(Op op, uint32_t name, uint32_t arg, std::string* message, uint32_t* select)
{
    *select = 0;
    message->clear();
    if (op == kOpCancel) {
        StopDrawing();
        g_pick = kOpCancel;
        *message = "Stopped";
        return false;
    }
    if (!InLevel()) {
        *message = "Not in a level";
        return false;
    }
    int index = Nodes::Find(name);
    if (index < 0)
        return false;
    const Nodes::Node node = Nodes::All()[index];
    Nodes::Family family = node.family;
    bool object = node.cls == PedestrianClass() || node.cls == VehicleClass();
    if (op == kOpAssignArm || op == kOpUnassign) {
        if (!object) {
            *message = "Only peds and cars have paths";
            return false;
        }
        if (op == kOpAssignArm) {
            g_pick = kOpAssignArm;
            g_pickFrom = name;
            *message = "Click the first waypoint (Esc: cancel)";
            return false;
        }
        // (its links to anything but waypoints kept)
        State state = Current();
        std::vector<uint32_t> links;
        for (uint32_t other : LevelLinksNow(name)) {
            if (FamilyOfName(other) != Nodes::Family::Path)
                links.push_back(other);
        }
        SetLinkEdit(&state, name, links);
        *select = name;
        return Apply(state, message, "No path now - it is made again without one");
    }
    bool ledge = family == Nodes::Family::Ladder && IsLedgeNode(name);
    if (family == Nodes::Family::Ladder && !ledge)
        return LadderOp(op, name, node, arg, message, select);
    if (family != Nodes::Family::Rail && family != Nodes::Family::Path && !ledge) {
        *message = "Not a rail, ladder or path point";
        return false;
    }
    if (node.hidden) {
        *message = "Deleted";
        return false;
    }
    State state = Current();
    Graph graph = GraphOf(family);
    switch (op) {
    case kOpInsert: {
        const std::vector<uint32_t>& links = graph[name];
        if (links.empty()) {
            *message = "Last point: use Extend";
            return false;
        }
        V3 a = PlaceOf(name), b = PlaceOf(links.front());
        Point p = NewPoint(family, name, (a + b) * 0.5f, state);
        uint32_t made = Checksum(p.name);
        state.points.push_back(p);
        InsertAfter(&graph, name, made);
        PutGraph(&state, graph);
        *select = made;
        return Apply(state, message, "A point put halfway to the next (Ctrl+Z takes it away)");
    }
    case kOpDelete: {
        Remove(&graph, name);
        PutGraph(&state, graph);
        if (node.added) {
            state.points.erase(std::remove_if(state.points.begin(), state.points.end(),
                                              [&](const Point& p) { return Checksum(p.name) == name; }),
                               state.points.end());
        } else {
            state.dropped.push_back(name);
            std::sort(state.dropped.begin(), state.dropped.end());
            // (its own links stay as the level has them: it takes no part while dropped)
            state.links.erase(std::remove_if(state.links.begin(), state.links.end(),
                                             [&](const LinkEdit& l) { return l.name == name; }),
                              state.links.end());
        }
        return Apply(state, message, node.added ? "Point deleted" : "Point hidden");
    }
    case kOpUnlink: {
        if (!Unlink(&graph, name)) {
            *message = "It has no next point";
            return false;
        }
        PutGraph(&state, graph);
        *select = name;
        return Apply(state, message, "Unlinked from its next point: the chain is split there");
    }
    case kOpTerrain: {
        if (family != Nodes::Family::Rail) {
            *message = "Only rails have a terrain";
            return false;
        }
        std::string terrain;
        for (const std::string& t : GrindTerrainNames()) {
            if (Checksum(t) == arg)
                terrain = t;
        }
        if (terrain.empty() || !KnownTerrain(arg)) {
            *message = "Unknown terrain";
            return false;
        }
        for (uint32_t point : ChainOf(name)) {
            bool added = false;
            for (Point& p : state.points) {
                if (Checksum(p.name) == point) {
                    p.terrain = terrain;
                    added = true;
                }
            }
            if (added)
                continue;
            auto it = std::find_if(state.terrains.begin(), state.terrains.end(),
                                   [&](const TerrainEdit& t) { return t.name == point; });
            if (it != state.terrains.end())
                it->terrain = terrain;
            else
                state.terrains.push_back({point, terrain});
        }
        SortByName(&state.terrains);
        *select = name;
        return Apply(state, message, ("The rail's terrain is " + terrain + " now").c_str());
    }
    case kOpExtend: {
        if (!IsLast(graph, name) && !IsFirst(graph, name)) {
            *message = "Select an end point to extend";
            return false;
        }
        g_draw = family == Nodes::Family::Path ? Draw::Path : ledge ? Draw::Ledge : Draw::Rail;
        g_drawLast = name;
        g_pick = kOpCancel;
        *message = "Click to add points (Enter: finish)";
        return false;
    }
    case kOpTrickObject: {
        if (family != Nodes::Family::Rail) {
            *message = "Only rails can be trick objects";
            return false;
        }
        // (the drawn points of the rail: one cluster - the one they have, else the scenery piece's or the first's)
        std::vector<uint32_t> rail = ChainOf(name);
        uint32_t cluster = 0;
        for (const Point& p : state.points) {
            if (std::find(rail.begin(), rail.end(), Checksum(p.name)) != rail.end() && !cluster)
                cluster = p.cluster;
        }
        bool any = false;
        for (Point& p : state.points) {
            if (std::find(rail.begin(), rail.end(), Checksum(p.name)) == rail.end())
                continue;
            if (!cluster)
                cluster = Checksum(p.name);
            p.cluster = arg ? cluster : 0;
            any = true;
        }
        if (!any) {
            *message = "Original rails keep their trick object setting";
            return false;
        }
        *select = name;
        return Apply(state, message, arg ? "Trick object on"
                                         : "Trick object off");
    }
    case kOpGrindFlat: {
        if (family != Nodes::Family::Rail) {
            *message = "Only rails are ground";
            return false;
        }
        for (uint32_t point : ChainOf(name)) {
            bool added = false;
            for (Point& p : state.points) {
                if (Checksum(p.name) == point) {
                    p.grindFlat = arg != 0;
                    added = true;
                }
            }
            if (added)
                continue;
            auto it = std::find(state.grindFlat.begin(), state.grindFlat.end(), point);
            if (arg && it == state.grindFlat.end())
                state.grindFlat.push_back(point);
            else if (!arg && it != state.grindFlat.end())
                state.grindFlat.erase(it);
        }
        std::sort(state.grindFlat.begin(), state.grindFlat.end());
        *select = name;
        if (arg)
            Log("level editor: %s's rail grindable on flat ground - RailClear (0x0057C600) passes it as the park "
                "editor's level passes every rail; the game itself has no node flag for this",
                Objects::Label(name).c_str());
        return Apply(state, message, arg ? "Grind on flat ground on"
                                         : "Ground beside it checked again, as the game does");
    }
    case kOpSpeed:
    case kOpSpeedRail: {
        if (family != Nodes::Family::Rail) {
            *message = "Only rails have speed-ups";
            return false;
        }
        float rate = arg ? ClampSpeed(static_cast<float>(arg) / 10000.0f) : 0.0f;
        std::vector<uint32_t> points = op == kOpSpeedRail ? ChainOf(name) : std::vector<uint32_t>{name};
        int changed = 0;
        for (uint32_t point : points) {
            const Obj::CRailNode* rail = RailNodeNamed(point);
            if (op == kOpSpeedRail && (!rail || !rail->m_next))
                continue; // (a rail's last point has no segment of its own)
            bool added = false;
            for (Point& p : state.points) {
                if (Checksum(p.name) == point) {
                    p.speed = rate;
                    added = true;
                }
            }
            if (!added) {
                auto it = std::find_if(state.speeds.begin(), state.speeds.end(),
                                       [&](const SpeedEdit& e) { return e.name == point; });
                if (rate <= 0.0f && it != state.speeds.end())
                    state.speeds.erase(it);
                else if (rate > 0.0f && it != state.speeds.end())
                    it->rate = rate;
                else if (rate > 0.0f)
                    state.speeds.push_back({point, rate});
            }
            ++changed;
        }
        SortByName(&state.speeds);
        *select = name;
        char text[160];
        if (rate > 0.0f)
            std::snprintf(text, sizeof(text), "Speed-up on %d segment%s: %.1f%% per frame",
                          changed, changed == 1 ? "" : "s", rate * 100.0f);
        else
            std::snprintf(text, sizeof(text), "Speed-up removed from %d segment%s", changed, changed == 1 ? "" : "s");
        return Apply(state, message, text);
    }
    case kOpHang: {
        if (!ledge) {
            *message = "Only ledges have a side to hang from";
            return false;
        }
        bool any = false;
        for (uint32_t point : ChainOf(name)) {
            for (Point& p : state.points) {
                if (Checksum(p.name) == point) {
                    p.hang = static_cast<int>(arg) == kHangLeft || static_cast<int>(arg) == kHangRight ? static_cast<int>(arg)
                                                                                                    : 0;
                    any = true;
                }
            }
        }
        if (!any) {
            *message = "Original ledges keep their hang side";
            return false;
        }
        *select = name;
        return Apply(state, message, arg == static_cast<uint32_t>(kHangLeft)    ? "The ledge is hung from its left side"
                                     : arg == static_cast<uint32_t>(kHangRight) ? "The ledge is hung from its right side"
                                                                                 : "The game decides the ledge's side");
    }
    case kOpLinkArm: {
        if (!IsLast(graph, name) && !IsFirst(graph, name)) {
            *message = "Select an end point to link";
            return false;
        }
        g_pick = kOpLinkArm;
        g_pickFrom = name;
        *message = "Click the end to link to (Esc: cancel)";
        return false;
    }
    default:
        return false;
    }
}

void StartDrawing(Draw kind)
{
    g_draw = kind;
    g_drawLast = 0;
    g_pick = kOpCancel;
    g_ladderHaveFoot = false;
}
Draw Drawing() { return g_draw; }
void StopDrawing()
{
    g_draw = Draw::None;
    g_drawLast = 0;
    g_ladderHaveFoot = false;
}

bool DrawAt(V3 at, uint32_t end, std::string* message, uint32_t* select)
{
    *select = 0;
    if (g_draw == Draw::None || !InLevel())
        return false;
    if (end && !g_drawLast) {
        // (the first click on an existing end: drawing goes on from it)
        g_drawLast = end;
        *select = end;
        *message = "Click the next point";
        return false;
    }
    Nodes::Family family = g_draw == Draw::Path    ? Nodes::Family::Path
                           : g_draw == Draw::Ledge ? Nodes::Family::Ladder
                                                   : Nodes::Family::Rail;
    State state = Current();
    Graph graph = GraphOf(family);
    if (g_drawLast && !graph.count(g_drawLast))
        g_drawLast = 0; // (it went: a new chain)
    if (end && g_drawLast && end != g_drawLast) {
        // (onto an existing end: linked, and the drawing done)
        if (!Link(&graph, g_drawLast, end)) {
            *message = "Can't link: no free end";
            return false;
        }
        PutGraph(&state, graph);
        StopDrawing();
        *select = end;
        return Apply(state, message, "Linked onto that end - drawing finished");
    }
    Point p = NewPoint(family, g_drawLast, at, state);
    uint32_t made = Checksum(p.name);
    state.points.push_back(p);
    graph[made] = {};
    if (g_drawLast && !Extend(&graph, g_drawLast, made)) {
        *message = "Not an end point: starting a new chain";
        g_drawLast = 0;
    }
    PutGraph(&state, graph);
    if (!SetState(state)) {
        *message = "Not in a level";
        return false;
    }
    bool first = g_drawLast == 0;
    g_drawLast = made;
    *select = made;
    if (message->empty())
        *message = first ? (family == Nodes::Family::Path     ? "Path started (Enter: finish)"
                            : family == Nodes::Family::Ladder ? "Ledge started (Enter: finish)"
                                                              : "Rail started (Enter: finish)")
                         : "Point added - Enter or a right click finishes";
    return true;
}

bool AddRail(const std::vector<V3>& points, uint32_t cluster, std::string* message, uint32_t* select)
{
    *select = 0;
    if (points.size() < 2 || !InLevel()) {
        *message = points.size() < 2 ? "A rail needs two points" : "Not in a level";
        return false;
    }
    Draw drawing = g_draw;
    g_draw = Draw::None; // (NewPoint: a plain rail, whatever is being drawn)
    State state = Current();
    Graph graph = GraphOf(Nodes::Family::Rail);
    uint32_t last = 0, first = 0;
    for (V3 at : points) {
        Point p = NewPoint(Nodes::Family::Rail, last, at, state);
        uint32_t made = Checksum(p.name);
        if (!last)
            p.cluster = cluster ? cluster : made;
        state.points.push_back(p);
        graph[made] = {};
        if (last)
            Extend(&graph, last, made);
        else
            first = made;
        last = made;
    }
    g_draw = drawing;
    PutGraph(&state, graph);
    if (!SetState(state)) {
        *message = "Not in a level";
        return false;
    }
    *select = first;
    return true;
}

namespace {

// The game's collision ray (as level_editor.cpp's: a CFeeler, 0x00417760 / 0x00417710; the hit's normal at +0x24, its
// point at +0x34, the collision object hit at +0x54 and its checksum - the scenery piece's - at +0x58, as
// level_editor_scenery.cpp's CastStatic reads them).
bool CastRay(V3 from, V3 to, V3* hit, V3* normal, uint32_t* sector = nullptr)
{
    alignas(16) uint8_t feeler[0x100] = {};
    GameFunction<void*(__thiscall*)(void*)>(0x00417760)(feeler);
    Mth::Vector start = {from.x, from.y, from.z, 1.0f}, end = {to.x, to.y, to.z, 1.0f};
    if (!GameFunction<bool(__thiscall*)(void*, const Mth::Vector*, const Mth::Vector*, int)>(0x00417710)(feeler, &start,
                                                                                                        &end, 0))
        return false;
    const float* n = reinterpret_cast<const float*>(feeler + 0x24);
    const float* p = reinterpret_cast<const float*>(feeler + 0x34);
    *normal = {n[0], n[1], n[2]};
    *hit = {p[0], p[1], p[2]};
    if (sector)
        *sector = *reinterpret_cast<void* const*>(feeler + 0x54) ? *reinterpret_cast<const uint32_t*>(feeler + 0x58) : 0;
    return true;
}

// The ground's height below a point (within 70).
bool GroundBelow(V3 at, float* groundY)
{
    V3 h, n;
    if (!CastRay(at, {at.x, at.y - 70.0f, at.z}, &h, &n))
        return false;
    *groundY = h.y;
    return true;
}

} // namespace

bool LadderClick(V3 from, V3 dir, float maxT, std::string* message, uint32_t* select)
{
    *select = 0;
    if (g_draw != Draw::Ladder || !InLevel())
        return false;
    if (!g_ladderHaveFoot) {
        V3 hit, normal;
        if (!CastRay(from, from + dir * maxT, &hit, &normal)) {
            *message = "Click on a wall";
            return false;
        }
        V3 foot;
        if (!LadderFoot(hit, normal, kLadderOut, &GroundBelow, &foot)) {
            *message = "Click a wall to place the ladder's foot";
            return false;
        }
        g_ladderFoot = foot;
        g_ladderFacing = FacingIntoWall(normal);
        g_ladderHaveFoot = true;
        *message = "Click to place the top";
        return false;
    }
    V3 top;
    if (!LadderTop(g_ladderFoot, from, dir, &top)) {
        *message = "Top must be at least 24 units above the foot";
        return false;
    }
    // The ladder: its foot linked to its top, both facing into the wall (as THUG2's own ladders).
    State state = Current();
    Point foot = NewPoint(Nodes::Family::Ladder, 0, g_ladderFoot, state);
    foot.facing = g_ladderFacing;
    state.points.push_back(foot);
    Point upper = NewPoint(Nodes::Family::Ladder, 0, top, state);
    upper.facing = g_ladderFacing;
    state.points.back().links = {Checksum(upper.name)};
    state.points.push_back(upper);
    if (!SetState(state)) {
        *message = "Not in a level";
        return false;
    }
    StopDrawing();
    *select = Checksum(foot.name);
    char text[128];
    std::snprintf(text, sizeof(text), "Ladder added (%.0f high)",
                  static_cast<double>(top.y - g_ladderFoot.y));
    *message = text;
    Log("level editor: added %s / %s (a ladder, facing %.0f degrees)", foot.name.c_str(), upper.name.c_str(),
        static_cast<double>(NodeEdit::HeadingDegrees({0.0f, g_ladderFacing, 0.0f})));
    return true;
}

bool SnapPoint(V3 from, V3 dir, float maxT, bool free, V3* point, uint32_t* end)
{
    *end = 0;
    V3 hit, normal;
    if (!CastRay(from, from + dir * maxT, &hit, &normal, &g_snapSector))
        return false;
    *point = hit;
    if (!free) {
        bool snapped = false;
        *point = SnapToEdge(hit, normal, 32.0f,
                            [](V3 at, float* groundY) {
                                V3 h, n;
                                if (!CastRay(at, {at.x, at.y - 60.0f, at.z}, &h, &n))
                                    return false;
                                *groundY = h.y;
                                return true;
                            },
                            &snapped);
    }
    // A free end of a chain of the kind drawn, within 24: taken as it is.
    if (g_draw != Draw::None) {
        Nodes::Family family = g_draw == Draw::Path    ? Nodes::Family::Path
                               : g_draw == Draw::Ledge ? Nodes::Family::Ladder
                                                       : Nodes::Family::Rail;
        Graph graph = GraphOf(family);
        float best = 24.0f * 24.0f;
        for (const auto& entry : graph) {
            if (entry.first == g_drawLast || (!IsLast(graph, entry.first) && !IsFirst(graph, entry.first)))
                continue;
            if (g_draw == Draw::Ledge && !IsLedgeNode(entry.first))
                continue; // (a ledge links onto ledges, not ladders)
            V3 at = PlaceOf(entry.first), d = at - *point;
            float distance = Dot(d, d);
            if (distance < best) {
                best = distance;
                *end = entry.first;
            }
        }
        if (*end)
            *point = PlaceOf(*end);
    }
    return true;
}

bool WantsPick() { return g_pick == kOpLinkArm || g_pick == kOpAssignArm; }

bool Picked(uint32_t name, std::string* message, uint32_t* select)
{
    *select = 0;
    Op pick = g_pick;
    g_pick = kOpCancel;
    uint32_t from = g_pickFrom;
    if (pick == kOpLinkArm) {
        Nodes::Family family = FamilyOfName(from);
        if (FamilyOfName(name) != family) {
            *message = "Can only link chains of the same kind";
            return false;
        }
        State state = Current();
        Graph graph = GraphOf(family);
        if (!Link(&graph, from, name)) {
            *message = "Can't link: no free end";
            return false;
        }
        PutGraph(&state, graph);
        *select = from;
        return Apply(state, message, "Linked");
    }
    if (pick == kOpAssignArm) {
        if (FamilyOfName(name) != Nodes::Family::Path) {
            *message = "Click a path's waypoint";
            return false;
        }
        State state = Current();
        std::vector<uint32_t> links = {name};
        for (uint32_t other : LevelLinksNow(from)) {
            if (FamilyOfName(other) != Nodes::Family::Path && other != name)
                links.push_back(other);
        }
        SetLinkEdit(&state, from, links);
        *select = from;
        return Apply(state, message, "Path assigned");
    }
    return false;
}

const char* Waiting()
{
    if (g_pick == kOpLinkArm)
        return "Click the end to link to (Esc: cancel)";
    if (g_pick == kOpAssignArm)
        return "Click the first waypoint (Esc: cancel)";
    if (g_draw == Draw::Rail)
        return g_drawLast ? "Rail: click the next point (Enter: finish)"
                          : "Rail: click the first point";
    if (g_draw == Draw::Path)
        return g_drawLast ? "Path: click the next waypoint (Enter: finish)"
                          : "Path: click the first waypoint";
    if (g_draw == Draw::Ledge)
        return g_drawLast ? "Ledge: click the next point (Enter: finish)"
                          : "Ledge: click the first point on an edge";
    if (g_draw == Draw::Ladder)
        return g_ladderHaveFoot ? "Ladder: click to place the top"
                                : "Ladder: click a wall to place the foot";
    return nullptr;
}

View ViewOf(uint32_t name)
{
    View view;
    view.waiting = Waiting();
    int index = Nodes::Find(name);
    if (index < 0 || !InLevel())
        return view;
    const Nodes::Node& node = Nodes::All()[index];
    view.terrains = GrindTerrainNames();
    if (node.cls == PedestrianClass() || node.cls == VehicleClass()) {
        view.show = view.object = true;
        for (uint32_t link : LevelLinksNow(name)) {
            if (FamilyOfName(link) == Nodes::Family::Path)
                view.pathStart = Objects::Label(link);
        }
        return view;
    }
    view.rail = node.family == Nodes::Family::Rail;
    view.ledge = node.family == Nodes::Family::Ladder && IsLedgeNode(name);
    view.ladder = node.family == Nodes::Family::Ladder && !view.ledge;
    view.path = node.family == Nodes::Family::Path;
    if (!view.rail && !view.ladder && !view.path && !view.ledge)
        return view;
    view.show = true;
    view.added = node.added;
    view.points = static_cast<int>(ChainOf(name).size());
    if (node.hidden) {
        view.why = "Deleted";
        return view;
    }
    if (view.ladder) {
        view.canTurn = view.canDelete = true;
        view.facingDegrees = NodeEdit::HeadingDegrees({0.0f, FacingNow(name), 0.0f});
        view.turned = !node.added && g_facings.count(name) != 0;
        return view;
    }
    Graph graph = GraphOf(node.family);
    const std::vector<uint32_t>& links = graph[name];
    std::vector<uint32_t> before = Predecessors(graph, name);
    if (!links.empty())
        view.next = Objects::Label(links.front());
    if (!before.empty())
        view.prev = Objects::Label(before.front());
    view.canInsert = !links.empty();
    view.canUnlink = !links.empty();
    view.canExtend = IsLast(graph, name) || IsFirst(graph, name);
    view.canLink = view.canExtend;
    view.canDelete = true;
    if (view.ledge) {
        if (node.added) {
            for (const Point& p : AddedPoints()) {
                if (Checksum(p.name) == name)
                    view.hang = p.hang;
            }
        } else if (Script::CStruct* params = NodeStruct(name)) {
            static const uint32_t kLeft = Checksum("HangLeft"), kRight = Checksum("HangRight");
            view.hang = params->ContainsFlag(kLeft) ? kHangLeft : params->ContainsFlag(kRight) ? kHangRight : 0;
        }
    }
    if (view.rail) {
        auto speed = g_extras.find(name);
        auto levelSpeed = g_levelSpeeds.find(name);
        view.speed = speed != g_extras.end() ? speed->second.speed
                     : levelSpeed != g_levelSpeeds.end() ? levelSpeed->second
                                                         : 0.0f;
        Script::CStruct* own = NodeStruct(name);
        uint32_t script = 0;
        auto applied = g_speedApplied.find(name);
        if (applied != g_speedApplied.end())
            script = applied->second.originalScript;
        else if (own)
            own->GetChecksum(kTriggerScriptKey, &script, 0);
        if (script) {
            Triggers::Calls calls = Triggers::ScriptCalls(script);
            view.levelSpeed = calls.railSpeed;
            view.levelRate = calls.railRate;
        }
        auto extras = g_extras.find(name);
        if (node.added) {
            view.canTrickObject = true;
            view.trickObject = extras != g_extras.end() && extras->second.cluster;
            if (view.trickObject)
                view.cluster = Objects::Label(extras->second.cluster);
            view.grindFlat = extras != g_extras.end() && extras->second.grindFlat;
        } else {
            view.trickObject = node.trickObject;
            static const uint32_t kCluster = Checksum("Cluster"), kFlag = Checksum("ed_grind_flat");
            Script::CStruct* params = NodeStruct(name);
            uint32_t cluster = name;
            if (params)
                params->GetChecksum(kCluster, &cluster, 0);
            if (view.trickObject)
                view.cluster = Objects::Label(cluster);
            view.grindFlat = std::find(g_levelFlat.begin(), g_levelFlat.end(), name) != g_levelFlat.end() ||
                             (params && params->ContainsFlag(kFlag));
        }
        if (const Obj::CRailNode* rail = RailNodeNamed(name)) {
            for (size_t i = 0; i < view.terrains.size(); ++i) {
                uint8_t value = 0;
                if (TerrainByte(view.terrains[i], &value) && value == rail->m_terrain) {
                    view.terrain = static_cast<int>(i);
                    break;
                }
            }
        }
    }
    return view;
}

} // namespace Front::LevelEditor::Chains
