// NOT ORIGINAL - the level editor's chains: grind rails, ladders and paths (ours). See level_editor_chains.h.

#include "editor/level_editor_chains.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <initializer_list>
#include <set>
#include <unordered_map>
#include <vector>
#include <utility>
#include <array>

#include "editor/level_editor.h"
#include "base/core/checksums.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "editor/level_editor_fields.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_triggers.h"
#include "engine/lists/key_map.h"
#include "base/maths/mat4.h"
#include "base/maths/quat_rotation.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "engine/objects/rail_manager.h"
#include "engine/objects/trick_objects.h"
#include "game/commands/game_cfuncs.h"
#include "engine/script/load.h"
#include "engine/script/node_manager.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/sound/surface_kind.h"
#include "platform/run_log.h"
#include "game/skate/skate_module.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "base/maths/vec4.h"
#include "engine/script/qb.h"

namespace frontend::level_editor::chains {

// --- the pure parts ---------------------------------------------------------------------------------------------

namespace {

constexpr std::size_t kNumberTextSize = 16;
constexpr std::size_t kChecksumDigits = 8;   // hex digits of a checksum
constexpr std::size_t kMaxNameLength = 63;   // the longest chain name
constexpr std::size_t kPositionTextSize = 96;
constexpr std::size_t kFacingTextSize = 48;
constexpr std::size_t kSpeedTextSize = 48;
constexpr std::size_t kRateTextSize = 48;
constexpr std::size_t kScriptNameSize = 32; // "ed_railspeed_" and eight digits
constexpr std::size_t kFacingMessageSize = 96;
constexpr std::size_t kListTextSize = 160;
constexpr std::size_t kMessageTextSize = 128;
constexpr int kBitsPerByte = 8;
constexpr float kMinFlatNormalY = 0.7F; // a surface normal this upright is floor, not wall
constexpr float kTinyLength = 1e-6F;
constexpr float kGroundProbeRise = 4.0F;   // the ground under a ladder foot is looked for from this far above ...
constexpr float kGroundProbeDepth = 64.0F; // ... down to this far below it
constexpr float kParallelEpsilon = 1e-6F;  // lines this close to parallel have no nearest point
constexpr float kFacingEpsilon = 1e-4F;    // two facings closer than this are the same
constexpr float kSameSegmentTolerance = 0.01F;
constexpr float kDropProbeDepth = 70.0F;
constexpr float kEdgeSnapReach = 32.0F; // a point snaps to an edge within this
constexpr float kEdgeProbeDepth = 60.0F;
constexpr float kJoinReach = 24.0F; // a free chain end within this is joined

bool same_text(const std::string& a, const char* b)
{
    if (a.size() != std::strlen(b)) {
        return false;
    }
    const std::string_view wanted(b);
    for (size_t i = 0; i < a.size(); ++i) {
        char x = core::at(a, i);
        char y = core::at(wanted, i);
        if (x >= 'A' && x <= 'Z') {
            x = static_cast<char>(x - 'A' + 'a');
        }
        if (y >= 'A' && y <= 'Z') {
            y = static_cast<char>(y - 'A' + 'a');
        }
        if (x != y) {
            return false;
        }
    }
    return true;
}

bool same_v3(V3 a, V3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

void get_hex(std::ostringstream& out, uint32_t value)
{
    std::array<char, kNumberTextSize> text{};
    text::format_to(text, "%08X", value);
    out << text.data();
}

bool parse_hex(const std::string& text, uint32_t* value)
{
    if (text.size() != kChecksumDigits) {
        return false;
    }
    for (const char character : text) {
        if (std::isxdigit(static_cast<unsigned char>(character)) == 0) {
            return false;
        }
    }
    *value = static_cast<uint32_t>(std::strtoul(text.c_str(), nullptr, kHexBase));
    return true;
}

bool read_hex_word(std::istringstream& in, uint32_t* value)
{
    std::string word;
    return static_cast<bool>(in >> word) && parse_hex(word, value);
}

// "a,b,c" of 8-hex-digit names (none: false).
bool parse_names(const std::string& text, std::vector<uint32_t>* out)
{
    out->clear();
    size_t start = 0;
    while (start <= text.size()) {
        const size_t comma = text.find(',', start);
        const std::string word = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        uint32_t value = 0;
        if (!parse_hex(word, &value)) {
            return false;
        }
        out->push_back(value);
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return !out->empty();
}

void get_names(std::ostringstream& out, const std::vector<uint32_t>& names)
{
    for (size_t i = 0; i < names.size(); ++i) {
        if (i != 0U) {
            out << ',';
        }
        get_hex(out, core::at(names, i));
    }
}

bool valid_word(const std::string& name)
{
    if (name.empty() || name.size() > kMaxNameLength) {
        return false;
    }
    return std::ranges::all_of(name, [](const char character) {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9') || character == '_';
    });
}

bool read_triple(const std::string& text, V3* out)
{
    std::array<float, 3> v{};
    // (the text from the place read to: strtof's end pointer says how far it got)
    std::string at(text);
    for (int i = 0; i < 3; ++i) {
        char* end = nullptr;
        core::at(v, i) = std::strtof(at.c_str(), &end);
        if (end == at.c_str() || !std::isfinite(core::at(v, i))) {
            return false;
        }
        at.erase(0, static_cast<size_t>(end - at.c_str()));
        if (i < 2) {
            if (at.empty() || at.front() != ',') {
                return false;
            }
            at.erase(0, 1);
        }
    }
    if (!at.empty()) {
        return false;
    }
    *out = {.x = core::at(v, 0), .y = core::at(v, 1), .z = core::at(v, 2)};
    return true;
}

bool read_float(const std::string& text, float* out)
{
    char* end = nullptr;
    const float v = std::strtof(text.c_str(), &end);
    if (text.empty() || static_cast<size_t>(end - text.c_str()) != text.size() || !std::isfinite(v)) {
        return false;
    }
    *out = v;
    return true;
}

template <typename T>
void sort_by_name(std::vector<T>* list)
{
    std::sort(list->begin(), list->end(), [](const T& a, const T& b) { return a.name < b.name; });
}

} // namespace

bool same_point(const Point& a, const Point& b)
{
    return a.name == b.name && a.cls == b.cls && same_v3(a.pos, b.pos) && a.like == b.like && a.terrain == b.terrain &&
           a.links == b.links && a.facing == b.facing && a.cluster == b.cluster && a.grind_flat_ == b.grind_flat_ &&
           same_text(a.type, b.type.c_str()) && a.hang == b.hang && a.speed == b.speed;
}

bool same_state(const State& a, const State& b)
{
    if (a.points.size() != b.points.size() || a.links.size() != b.links.size() || a.dropped != b.dropped ||
        a.terrains.size() != b.terrains.size() || a.facings.size() != b.facings.size() ||
        a.grind_flat_ != b.grind_flat_ || a.speeds.size() != b.speeds.size()) {
        return false;
    }
    for (size_t i = 0; i < a.speeds.size(); ++i) {
        if (core::at(a.speeds, i).name != core::at(b.speeds, i).name ||
            core::at(a.speeds, i).rate != core::at(b.speeds, i).rate) {
            return false;
        }
    }
    for (size_t i = 0; i < a.facings.size(); ++i) {
        if (core::at(a.facings, i).name != core::at(b.facings, i).name ||
            core::at(a.facings, i).facing != core::at(b.facings, i).facing) {
            return false;
        }
    }
    for (size_t i = 0; i < a.points.size(); ++i) {
        if (!same_point(core::at(a.points, i), core::at(b.points, i))) {
            return false;
        }
    }
    for (size_t i = 0; i < a.links.size(); ++i) {
        if (core::at(a.links, i).name != core::at(b.links, i).name ||
            core::at(a.links, i).links != core::at(b.links, i).links) {
            return false;
        }
    }
    for (size_t i = 0; i < a.terrains.size(); ++i) {
        if (core::at(a.terrains, i).name != core::at(b.terrains, i).name ||
            core::at(a.terrains, i).terrain != core::at(b.terrains, i).terrain) {
            return false;
        }
    }
    return true;
}

bool vacant(const State& state)
{
    return state.points.empty() && state.links.empty() && state.dropped.empty() && state.terrains.empty() &&
           state.facings.empty() && state.grind_flat_.empty() && state.speeds.empty();
}

bool is_chain_class(const std::string& cls)
{
    return same_text(cls, "RailNode") || same_text(cls, "Waypoint") || same_text(cls, "ClimbingNode");
}

std::string write_records(const State& state)
{
    std::ostringstream out;
    for (const Point& chain_point : state.points) {
        std::array<char, kPositionTextSize> pos{};
        text::format_to(pos, " pos=%.9g,%.9g,%.9g", chain_point.pos.x, chain_point.pos.y, chain_point.pos.z);
        out << "point " << chain_point.name << ' ' << chain_point.cls << pos.data();
        if (chain_point.like != 0U) {
            out << " like=";
            get_hex(out, chain_point.like);
        }
        if (!chain_point.terrain.empty()) {
            out << " terrain=" << chain_point.terrain;
        }
        if (!chain_point.links.empty()) {
            out << " links=";
            get_names(out, chain_point.links);
        }
        if (same_text(chain_point.cls, "ClimbingNode") || chain_point.facing != 0.0F) {
            std::array<char, kFacingTextSize> facing{};
            text::format_to(facing, " facing=%.9g", chain_point.facing);
            out << facing.data();
        }
        if (chain_point.cluster != 0U) {
            out << " cluster=";
            get_hex(out, chain_point.cluster);
        }
        if (chain_point.grind_flat_) {
            out << " grindflat=1";
        }
        if (is_ledge(chain_point)) {
            out << " type=Ledge";
        }
        if (chain_point.hang != 0) {
            out << " hang=" << hang_word(chain_point.hang);
        }
        if (chain_point.speed > 0.0F) {
            std::array<char, kSpeedTextSize> speed{};
            text::format_to(speed, " speed=%.9g", chain_point.speed);
            out << speed.data();
        }
        out << '\n';
    }
    for (const LinkEdit& link_edit : state.links) {
        out << "links ";
        get_hex(out, link_edit.name);
        out << ' ';
        if (link_edit.links.empty()) {
            out << '-';
        } else {
            get_names(out, link_edit.links);
        }
        out << '\n';
    }
    for (const uint32_t name : state.dropped) {
        out << "drop ";
        get_hex(out, name);
        out << '\n';
    }
    for (const TerrainEdit& terrain_edit : state.terrains) {
        out << "terrain ";
        get_hex(out, terrain_edit.name);
        out << ' ' << terrain_edit.terrain << '\n';
    }
    for (const FacingEdit& facing_edit : state.facings) {
        std::array<char, kFacingTextSize> facing{};
        text::format_to(facing, " %.9g", facing_edit.facing);
        out << "facing ";
        get_hex(out, facing_edit.name);
        out << facing.data() << '\n';
    }
    for (const uint32_t name : state.grind_flat_) {
        out << "grindflat ";
        get_hex(out, name);
        out << '\n';
    }
    for (const SpeedEdit& speed_edit : state.speeds) {
        std::array<char, kRateTextSize> rate{};
        text::format_to(rate, " %.9g", speed_edit.rate);
        out << "railspeed ";
        get_hex(out, speed_edit.name);
        out << rate.data() << '\n';
    }
    return out.str();
}

bool is_record(const std::string& kind)
{
    return kind == "point" || kind == "links" || kind == "drop" || kind == "terrain" || kind == "facing" ||
           kind == "grindflat" || kind == "railspeed";
}

namespace {

// A point record's fields (key=value), each read into the point; false when its value is wrong.
bool read_point_pos(const std::string& value, Point& chain_point) { return read_triple(value, &chain_point.pos); }
bool read_point_like(const std::string& value, Point& chain_point) { return parse_hex(value, &chain_point.like); }
bool read_point_terrain(const std::string& value, Point& chain_point)
{
    if (!valid_word(value)) {
        return false;
    }
    chain_point.terrain = value;
    return true;
}
bool read_point_links(const std::string& value, Point& chain_point) { return parse_names(value, &chain_point.links); }
bool read_point_facing(const std::string& value, Point& chain_point) { return read_float(value, &chain_point.facing); }
bool read_point_cluster(const std::string& value, Point& chain_point) { return parse_hex(value, &chain_point.cluster); }
bool read_point_grind_flat(const std::string& value, Point& chain_point)
{
    if (value != "0" && value != "1") {
        return false;
    }
    chain_point.grind_flat_ = value == "1";
    return true;
}
// (a ClimbingNode's: Ledge - a ladder's Type isn't written)
bool read_point_type(const std::string& value, Point& chain_point)
{
    if (!same_text(value, "Ledge") || !same_text(chain_point.cls, "ClimbingNode")) {
        return false;
    }
    chain_point.type = "Ledge";
    return true;
}
bool read_point_hang(const std::string& value, Point& chain_point)
{
    if (value == hang_word(kHangLeft)) {
        chain_point.hang = kHangLeft;
    } else if (value == hang_word(kHangRight)) {
        chain_point.hang = kHangRight;
    } else {
        return false;
    }
    return true;
}
bool read_point_speed(const std::string& value, Point& chain_point)
{
    return read_float(value, &chain_point.speed) && chain_point.speed >= kSpeedMin && chain_point.speed <= kSpeedMax &&
           same_text(chain_point.cls, "RailNode");
}

struct PointField {
    const char* key;
    bool (*read)(const std::string& value, Point& chain_point);
};
constexpr auto kPointFields = std::to_array<PointField>({
    {.key = "pos", .read = read_point_pos},
    {.key = "like", .read = read_point_like},
    {.key = "terrain", .read = read_point_terrain},
    {.key = "links", .read = read_point_links},
    {.key = "facing", .read = read_point_facing},
    {.key = "cluster", .read = read_point_cluster},
    {.key = "grindflat", .read = read_point_grind_flat},
    {.key = "type", .read = read_point_type},
    {.key = "hang", .read = read_point_hang},
    {.key = "speed", .read = read_point_speed},
});

// point <name> <class> key=value...: every key once, pos= given, a name no other point has.
bool read_point_record(std::istringstream& in, State* state)
{
    Point chain_point;
    if (!(in >> chain_point.name >> chain_point.cls) || !valid_word(chain_point.name) ||
        !is_chain_class(chain_point.cls)) {
        return false;
    }
    std::vector<std::string> seen;
    std::string field;
    while (in >> field) {
        const size_t equals = field.find('=');
        if (equals == std::string::npos || equals == 0) {
            return false;
        }
        const std::string key = field.substr(0, equals);
        const std::string value = field.substr(equals + 1);
        if (std::ranges::find(seen, key) != seen.end()) {
            return false;
        }
        seen.push_back(key);
        const auto reader =
            std::ranges::find_if(kPointFields, [&](const PointField& point_field) { return key == point_field.key; });
        if (reader != kPointFields.end() && !reader->read(value, chain_point)) {
            return false;
        } // (others: a later version's - skipped)
    }
    if (std::ranges::find(seen, "pos") == seen.end()) {
        return false;
    }
    for (const Point& existing_point : state->points) {
        if (same_text(existing_point.name, chain_point.name.c_str())) {
            return false;
        }
    }
    state->points.push_back(chain_point);
    return true;
}

// links <name> <names or ->: once per point.
bool read_links_record(std::istringstream& in, State* state)
{
    LinkEdit link_edit = {};
    std::string value;
    if (!read_hex_word(in, &link_edit.name) || !(in >> value)) {
        return false;
    }
    if (value != "-" && !parse_names(value, &link_edit.links)) {
        return false;
    }
    for (const LinkEdit& other : state->links) {
        if (other.name == link_edit.name) {
            return false;
        }
    }
    state->links.push_back(link_edit);
    sort_by_name(&state->links);
    return true;
}

// drop <name> (a second one changes nothing).
bool read_drop_record(std::istringstream& in, State* state)
{
    uint32_t name = 0;
    if (!read_hex_word(in, &name)) {
        return false;
    }
    if (std::ranges::find(state->dropped, name) == state->dropped.end()) {
        state->dropped.push_back(name);
        std::ranges::sort(state->dropped);
    }
    return true;
}

// terrain <name> <terrain>: once per point.
bool read_terrain_record(std::istringstream& in, State* state)
{
    TerrainEdit terrain_edit = {};
    if (!read_hex_word(in, &terrain_edit.name) || !(in >> terrain_edit.terrain) || !valid_word(terrain_edit.terrain)) {
        return false;
    }
    for (const TerrainEdit& other : state->terrains) {
        if (other.name == terrain_edit.name) {
            return false;
        }
    }
    state->terrains.push_back(terrain_edit);
    sort_by_name(&state->terrains);
    return true;
}

// facing <name> <angle>: once per point.
bool read_facing_record(std::istringstream& in, State* state)
{
    FacingEdit facing_edit = {};
    std::string value;
    if (!read_hex_word(in, &facing_edit.name) || !(in >> value) || !read_float(value, &facing_edit.facing)) {
        return false;
    }
    for (const FacingEdit& other : state->facings) {
        if (other.name == facing_edit.name) {
            return false;
        }
    }
    state->facings.push_back(facing_edit);
    sort_by_name(&state->facings);
    return true;
}

// grindflat <name> (a second one changes nothing).
bool read_grind_flat_record(std::istringstream& in, State* state)
{
    uint32_t name = 0;
    if (!read_hex_word(in, &name)) {
        return false;
    }
    if (std::ranges::find(state->grind_flat_, name) == state->grind_flat_.end()) {
        state->grind_flat_.push_back(name);
        std::ranges::sort(state->grind_flat_);
    }
    return true;
}

// railspeed <name> <rate>: once per point, the rate in range.
bool read_rail_speed_record(std::istringstream& in, State* state)
{
    SpeedEdit speed_edit = {};
    std::string value;
    if (!read_hex_word(in, &speed_edit.name) || !(in >> value) || !read_float(value, &speed_edit.rate) ||
        !(speed_edit.rate >= kSpeedMin && speed_edit.rate <= kSpeedMax)) {
        return false;
    }
    for (const SpeedEdit& other : state->speeds) {
        if (other.name == speed_edit.name) {
            return false;
        }
    }
    state->speeds.push_back(speed_edit);
    sort_by_name(&state->speeds);
    return true;
}

} // namespace

bool read_record(const std::string& kind, std::istringstream& in, State* state)
{
    if (kind == "point") {
        return read_point_record(in, state);
    }
    if (kind == "links") {
        return read_links_record(in, state);
    }
    if (kind == "drop") {
        return read_drop_record(in, state);
    }
    if (kind == "terrain") {
        return read_terrain_record(in, state);
    }
    if (kind == "facing") {
        return read_facing_record(in, state);
    }
    if (kind == "grindflat") {
        return read_grind_flat_record(in, state);
    }
    if (kind == "railspeed") {
        return read_rail_speed_record(in, state);
    }
    return false;
}

float clamp_speed(float rate)
{
    if (!(rate >= kSpeedMin)) {
        return kSpeedMin;
    }
    return rate > kSpeedMax ? kSpeedMax : rate;
}

bool is_ledge(const Point& point) { return same_text(point.cls, "ClimbingNode") && same_text(point.type, "Ledge"); }

const char* hang_word(int hang)
{
    if (hang == kHangLeft) {
        return "left";
    }
    if (hang == kHangRight) {
        return "right";
    }
    return "";
}

const std::vector<uint8_t>& rail_speed_qb()
{
    // (NeverScript's output for the source in level_editor_chains.h - the self-test walks it token by token)
    static const std::vector<uint8_t> kQb = {
        0x23, 0x16, 0x7D, 0x9E, 0xC1, 0xF6, 0x16, 0xC6, 0xC0, 0x13, 0x20, 0x07, 0x1A, 0xCD, 0xCC, 0x4C, 0x3D, 0x01,
        0x47, 0x4C, 0x00, 0x16, 0x82, 0x73, 0x71, 0x3A, 0x05, 0x17, 0x03, 0x00, 0x00, 0x00, 0x09, 0x17, 0x0D, 0x00,
        0x00, 0x00, 0x09, 0x17, 0xFF, 0xFF, 0xFF, 0xFF, 0x06, 0x01, 0x16, 0x2D, 0xC0, 0x0D, 0xCD, 0x16, 0xF9, 0x81,
        0xDC, 0xA1, 0x07, 0x16, 0x6F, 0x9E, 0xFC, 0x68, 0x01, 0x16, 0xC2, 0xE5, 0xA4, 0x23, 0x16, 0x6F, 0x9E, 0xFC,
        0x68, 0x16, 0x0C, 0xF1, 0x31, 0x70, 0x07, 0x03, 0x16, 0xC6, 0xC0, 0x13, 0x20, 0x07, 0x2D, 0x16, 0xC6, 0xC0,
        0x13, 0x20, 0x04, 0x01, 0x28, 0x01, 0x24, 0x01, 0x23, 0x16, 0x6F, 0x9E, 0xFC, 0x68, 0x16, 0xC6, 0xC0, 0x13,
        0x20, 0x07, 0x1A, 0xCD, 0xCC, 0x4C, 0x3D, 0x01, 0x16, 0x75, 0x26, 0x81, 0x20, 0x07, 0x17, 0x00, 0x00, 0x00,
        0x00, 0x01, 0x20, 0x01, 0x47, 0x83, 0x00, 0x16, 0x62, 0x1E, 0x85, 0xE9, 0x01, 0x16, 0x2D, 0xAD, 0x0A, 0xE9,
        0x01, 0x16, 0xD0, 0x6F, 0x61, 0xA4, 0x07, 0x0E, 0x2D, 0x16, 0x53, 0x19, 0x26, 0x7F, 0x08, 0x1E, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x01, 0x47, 0x36, 0x00, 0x0E, 0x2D, 0x16,
        0xD0, 0x6F, 0x61, 0xA4, 0x14, 0x2D, 0x16, 0x75, 0x26, 0x81, 0x20, 0x0F, 0x01, 0x16, 0x4A, 0xAC, 0xCA, 0xC0,
        0x01, 0x16, 0x9B, 0x93, 0x3B, 0x38, 0x0E, 0x2D, 0x16, 0x09, 0x01, 0xD9, 0xF0, 0x0D, 0x0E, 0x1A, 0x00, 0x00,
        0x80, 0x3F, 0x0B, 0x2D, 0x16, 0xC6, 0xC0, 0x13, 0x20, 0x0F, 0x0F, 0x01, 0x28, 0x01, 0x16, 0x75, 0x26, 0x81,
        0x20, 0x07, 0x2D, 0x16, 0xD0, 0x6F, 0x61, 0xA4, 0x01, 0x16, 0x1A, 0x7C, 0x11, 0x82, 0x17, 0x01, 0x00, 0x00,
        0x00, 0x16, 0x05, 0xDF, 0x37, 0xFC, 0x01, 0x48, 0x06, 0x00, 0x01, 0x22, 0x01, 0x28, 0x01, 0x21, 0x01, 0x24,
        0x01, 0x2B, 0x7D, 0x9E, 0xC1, 0xF6, 0x65, 0x64, 0x5F, 0x52, 0x61, 0x69, 0x6C, 0x53, 0x70, 0x65, 0x65, 0x64,
        0x00, 0x2B, 0xF9, 0x81, 0xDC, 0xA1, 0x6E, 0x61, 0x6D, 0x65, 0x00, 0x2B, 0xC2, 0xE5, 0xA4, 0x23, 0x4F, 0x62,
        0x6A, 0x5F, 0x53, 0x70, 0x61, 0x77, 0x6E, 0x53, 0x63, 0x72, 0x69, 0x70, 0x74, 0x00, 0x2B, 0x9B, 0x93, 0x3B,
        0x38, 0x53, 0x65, 0x74, 0x53, 0x70, 0x65, 0x65, 0x64, 0x00, 0x2B, 0x09, 0x01, 0xD9, 0xF0, 0x73, 0x70, 0x65,
        0x65, 0x64, 0x00, 0x2B, 0x75, 0x26, 0x81, 0x20, 0x4F, 0x6C, 0x64, 0x50, 0x6F, 0x73, 0x00, 0x2B, 0x62, 0x1E,
        0x85, 0xE9, 0x4F, 0x6E, 0x52, 0x61, 0x69, 0x6C, 0x00, 0x2B, 0x4A, 0xAC, 0xCA, 0xC0, 0x47, 0x65, 0x74, 0x53,
        0x70, 0x65, 0x65, 0x64, 0x00, 0x2B, 0xC6, 0xC0, 0x13, 0x20, 0x72, 0x61, 0x74, 0x65, 0x00, 0x2B, 0x2D, 0xC0,
        0x0D, 0xCD, 0x4B, 0x69, 0x6C, 0x6C, 0x53, 0x70, 0x61, 0x77, 0x6E, 0x65, 0x64, 0x53, 0x63, 0x72, 0x69, 0x70,
        0x74, 0x00, 0x2B, 0x6F, 0x9E, 0xFC, 0x68, 0x65, 0x64, 0x5F, 0x52, 0x61, 0x69, 0x6C, 0x53, 0x70, 0x65, 0x65,
        0x64, 0x4C, 0x6F, 0x6F, 0x70, 0x00, 0x2B, 0xD0, 0x6F, 0x61, 0xA4, 0x4E, 0x65, 0x77, 0x50, 0x6F, 0x73, 0x00,
        0x2B, 0x82, 0x73, 0x71, 0x3A, 0x54, 0x72, 0x69, 0x67, 0x67, 0x65, 0x72, 0x54, 0x79, 0x70, 0x65, 0x00, 0x2B,
        0x0C, 0xF1, 0x31, 0x70, 0x70, 0x61, 0x72, 0x61, 0x6D, 0x73, 0x00, 0x2B, 0x2D, 0xAD, 0x0A, 0xE9, 0x4F, 0x62,
        0x6A, 0x5F, 0x47, 0x65, 0x74, 0x50, 0x6F, 0x73, 0x69, 0x74, 0x69, 0x6F, 0x6E, 0x00, 0x2B, 0x53, 0x19, 0x26,
        0x7F, 0x50, 0x6F, 0x73, 0x00, 0x2B, 0x1A, 0x7C, 0x11, 0x82, 0x77, 0x61, 0x69, 0x74, 0x00, 0x2B, 0x05, 0xDF,
        0x37, 0xFC, 0x67, 0x61, 0x6D, 0x65, 0x66, 0x72, 0x61, 0x6D, 0x65, 0x00, 0x00,
    };
    return kQb;
}

std::string speed_script_name(uint32_t node)
{
    std::array<char, kScriptNameSize> text{};
    text::format_to(text, "ed_railspeed_%08X", node);
    return text.data();
}

namespace {
void get_put4(std::vector<uint8_t>* out, uint32_t value)
{
    for (int i = 0; i < 4; ++i) {
        out->push_back(static_cast<uint8_t>(value >> (kBitsPerByte * i)));
    }
}
void put_name(std::vector<uint8_t>* out, uint32_t checksum)
{
    out->push_back(script::token_byte(script::QbToken::kName));
    get_put4(out, checksum);
}
} // namespace

std::vector<uint8_t> speed_body(SpeedChange speed_change)
{
    const auto original = speed_change.original;
    const auto rate = speed_change.rate;
    std::vector<uint8_t> out;
    if (original != 0U) {
        put_name(&out, original);
        out.push_back(script::token_byte(script::QbToken::kAllArguments)); // (<...>: its arguments passed on)
        out.push_back(0x01);
    }
    put_name(&out, checksums::hash_from_text("ed_RailSpeed"));
    put_name(&out, checksums::hash_from_text("rate"));
    out.push_back(script::token_byte(script::QbToken::kEquals));
    out.push_back(script::token_byte(script::QbToken::kFloat));
    auto bits = std::bit_cast<uint32_t>(rate);
    get_put4(&out, bits);
    out.push_back(0x01);
    return out;
}

std::vector<uint8_t> speed_qb_file(const std::vector<SpeedScript>& scripts)
{
    std::vector<uint8_t> out;
    for (const SpeedScript& speed_script : scripts) {
        out.push_back(script::token_byte(script::QbToken::kScriptStart));
        put_name(&out, checksums::hash_from_text(speed_script_name(speed_script.node).c_str()));
        out.push_back(0x01);
        std::vector<uint8_t> body = speed_body({.original = speed_script.original, .rate = speed_script.rate});
        out.insert(out.end(), body.begin(), body.end());
        out.push_back(script::token_byte(script::QbToken::kScriptEnd));
        out.push_back(0x01);
    }
    for (const SpeedScript& speed_script : scripts) {
        std::string name = speed_script_name(speed_script.node);
        out.push_back(script::token_byte(script::QbToken::kChecksumName));
        get_put4(&out, checksums::hash_from_text(name.c_str()));
        out.insert(out.end(), name.begin(), name.end());
        out.push_back(0x00);
    }
    out.push_back(0x00);
    return out;
}

std::vector<uint32_t> predecessors(const Graph& graph, uint32_t name)
{
    std::vector<uint32_t> found;
    for (const auto& entry : graph) {
        if (entry.first != name && std::ranges::find(entry.second, name) != entry.second.end()) {
            found.push_back(entry.first);
        }
    }
    return found;
}

namespace {
const std::vector<uint32_t>& links_of(const Graph& graph, uint32_t name)
{
    static const std::vector<uint32_t> kNone;
    auto it = graph.find(name);
    return it == graph.end() ? kNone : it->second;
}
} // namespace

bool is_last(const Graph& graph, uint32_t name) { return links_of(graph, name).empty(); }
bool is_first(const Graph& graph, uint32_t name) { return predecessors(graph, name).empty(); }

bool extend(Graph* graph, uint32_t from, uint32_t point)
{
    if (from == point || graph->contains(point)) {
        return false;
    }
    if (is_last(*graph, from)) {
        (*graph)[from] = {point};
        (*graph)[point] = {};
        return true;
    }
    if (is_first(*graph, from)) {
        (*graph)[point] = {from};
        return true;
    }
    return false;
}

bool insert_after(Graph* graph, uint32_t from, uint32_t point)
{
    auto it = graph->find(from);
    if (it == graph->end() || it->second.empty() || from == point || graph->contains(point)) {
        return false;
    }
    const uint32_t next = it->second.front();
    it->second.front() = point;
    (*graph)[point] = {next};
    return true;
}

void drop_out(Graph* graph, uint32_t name)
{
    std::vector<uint32_t> own = links_of(*graph, name);
    const uint32_t next = own.empty() ? 0 : own.front();
    for (auto& entry : *graph) {
        if (entry.first == name) {
            continue;
        }
        std::vector<uint32_t>& links = entry.second;
        auto at = std::ranges::find(links, name);
        if (at == links.end()) {
            continue;
        }
        const bool already = (next != 0U) && std::ranges::find(links, next) != links.end();
        if ((next != 0U) && !already && next != entry.first) {
            *at = next;
        } else {
            links.erase(at);
        }
    }
    (*graph)[name] = {};
}

bool get_link(Graph* graph, uint32_t a, uint32_t b)
{
    if (a == b) {
        return false;
    }
    if (is_last(*graph, a)) {
        (*graph)[a] = {b};
        if (!graph->contains(b)) {
            (*graph)[b] = {};
        }
        return true;
    }
    if (is_last(*graph, b)) {
        (*graph)[b] = {a};
        return true;
    }
    return false;
}

bool uncouple(Graph* graph, uint32_t name)
{
    auto it = graph->find(name);
    if (it == graph->end() || it->second.empty()) {
        return false;
    }
    it->second.erase(it->second.begin());
    return true;
}

std::vector<uint32_t> get_connected(const Graph& graph, uint32_t name)
{
    std::vector<uint32_t> found = {name};
    for (size_t i = 0; i < found.size(); ++i) {
        const uint32_t at = core::at(found, i);
        std::vector<uint32_t> near = links_of(graph, at);
        for (const uint32_t predecessor : predecessors(graph, at)) {
            near.push_back(predecessor);
        }
        for (const uint32_t count : near) {
            if (std::ranges::find(found, count) == found.end()) {
                found.push_back(count);
            }
        }
    }
    return found;
}

V3 snap_to_edge(V3 hit, V3 normal, float reach, const std::function<bool(V3 from, float* ground_y)>& ground_at,
                bool* snapped)
{
    *snapped = false;
    if (normal.y < kMinFlatNormalY) {
        return hit;
    }
    constexpr float kStep = 4.0F;
    constexpr float kDrop = 8.0F;
    float best = reach + 1.0F;
    V3 best_at = hit;
    constexpr float kHalf = 0.70710678F;
    static const std::array<V3, 8> kDirections = {{{.x = 0.0F, .y = 0.0F, .z = 1.0F},
                                                   {.x = kHalf, .y = 0.0F, .z = kHalf},
                                                   {.x = 1.0F, .y = 0.0F, .z = 0.0F},
                                                   {.x = kHalf, .y = 0.0F, .z = -kHalf},
                                                   {.x = 0.0F, .y = 0.0F, .z = -1.0F},
                                                   {.x = -kHalf, .y = 0.0F, .z = -kHalf},
                                                   {.x = -1.0F, .y = 0.0F, .z = 0.0F},
                                                   {.x = -kHalf, .y = 0.0F, .z = kHalf}}};
    for (const V3& dir : kDirections) {
        for (int step = 1;; ++step) {
            const float fraction = static_cast<float>(step) * kStep;
            if (!(fraction <= reach)) {
                break;
            }
            const V3 probe = hit + dir * fraction;
            float ground = 0.0F;
            const bool have = ground_at({.x = probe.x, .y = hit.y + 20.0F, .z = probe.z}, &ground);
            if (!have || ground < hit.y - kDrop) {
                const float edge = fraction - (kStep * 0.5F);
                if (edge < best) {
                    best = edge;
                    best_at = hit + dir * edge;
                }
                break;
            }
        }
    }
    if (best > reach) {
        return hit;
    }
    *snapped = true;
    return {.x = best_at.x, .y = hit.y, .z = best_at.z};
}

const std::vector<std::string>& grind_terrain_names()
{
    // (scripts\game\terrain.qb's Terrain_Types, the TERRAIN_GRIND... entries in its order - the names only: the game
    // checks them against its own table before one is used, TerrainChecksums)
    static const std::vector<std::string> kNames = {
        "TERRAIN_GRINDTRAIN",     "TERRAIN_GRINDROPE",           "TERRAIN_GRINDWIRE",
        "TERRAIN_GRINDCONC",      "TERRAIN_GRINDROUNDMETALPOLE", "TERRAIN_GRINDCHAINLINK",
        "TERRAIN_GRINDMETAL",     "TERRAIN_GRINDWOODRAILING",    "TERRAIN_GRINDWOODLOG",
        "TERRAIN_GRINDWOOD",      "TERRAIN_GRINDPLASTIC",        "TERRAIN_GRINDELECTRICWIRE",
        "TERRAIN_GRINDCABLE",     "TERRAIN_GRINDCHAIN",          "TERRAIN_GRINDPLASTICBARRIER",
        "TERRAIN_GRINDNEONLIGHT", "TERRAIN_GRINDGLASSMONSTER",   "TERRAIN_GRINDBANYONTREE",
        "TERRAIN_GRINDBRASSRAIL", "TERRAIN_GRINDCATWALK",        "TERRAIN_GRINDTANKTURRET",
    };
    return kNames;
}

float facing_into_wall(V3 normal)
{
    const float length = std::sqrt((normal.x * normal.x) + (normal.z * normal.z));
    if (!(length > kTinyLength)) {
        return 0.0F;
    }
    return std::atan2(-normal.x / length, -normal.z / length);
}

V3 facing_direction(float facing) { return {.x = std::sin(facing), .y = 0.0F, .z = std::cos(facing)}; }

bool ladder_foot(V3 hit, V3 normal, float out, const std::function<bool(V3 from, float* ground_y)>& ground_at, V3* foot)
{
    if (!(std::fabs(normal.y) < kMinFlatNormalY)) {
        return false;
    }
    const float length = std::sqrt((normal.x * normal.x) + (normal.z * normal.z));
    if (!(length > kTinyLength)) {
        return false;
    }
    V3 at = {.x = hit.x + (normal.x / length * out), .y = hit.y, .z = hit.z + (normal.z / length * out)};
    float ground = 0.0F;
    if (ground_at({.x = at.x, .y = at.y + kGroundProbeRise, .z = at.z}, &ground) && ground <= at.y + kGroundProbeRise &&
        ground >= at.y - kGroundProbeDepth) {
        at.y = ground;
    }
    *foot = at;
    return true;
}

bool ladder_top(V3 foot, V3 from, V3 dir, V3* top)
{
    // The closest points of the line foot + t (0, 1, 0) and the ray from + s dir.
    const V3 w = foot - from;
    const float dir_y = dir.y;
    const float dir_dir = get_dot(dir, dir);
    const float w_y = w.y;
    const float dir_w = get_dot(dir, w);
    const float denominator = dir_dir - (dir_y * dir_y);
    if (!(denominator > kParallelEpsilon * dir_dir)) {
        return false; // (looking straight up or down the line)
    }
    const float fraction = ((dir_y * dir_w) - (dir_dir * w_y)) / denominator;
    if (!(fraction >= kLadderMin)) {
        return false;
    }
    *top = {.x = foot.x, .y = foot.y + fraction, .z = foot.z};
    return true;
}

bool same_segment(V3 a, V3 b, V3 other_a, V3 other_b, float tolerance)
{
    auto near = [tolerance](V3 x, V3 y) {
        return std::fabs(x.x - y.x) <= tolerance && std::fabs(x.y - y.y) <= tolerance &&
               std::fabs(x.z - y.z) <= tolerance;
    };
    return (near(a, other_a) && near(b, other_b)) || (near(a, other_b) && near(b, other_a));
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kClass = 0x12B4E660;          // class
constexpr uint32_t kLinks = 0x2E7D5EE7;          // links
constexpr uint32_t kPos = 0x7F261953;            // pos
constexpr uint32_t kSurfaceKindKey = 0x54CF8532; // TerrainType

uint32_t name_hash(const std::string& text) { return checksums::hash_from_text(text.c_str()); }

bool in_level() { return level_nodes::node_manager_instance() != nullptr && scenery::main_scene() != nullptr; }

script::Struct* look_up_node_params(uint32_t name)
{
    return (name != 0U) && level_nodes::node_manager_instance() ? level_nodes::fetch_vertex(name, false) : nullptr;
}

// The level's rail manager (modules::Skate +0x24; null when there is none or its rails ride an object).
objects::GrindRailHub* get_rails()
{
    const modules::Skate* skate = modules::Skate::sole();
    auto* manager = skate ? skate->rail_manager().get() : nullptr;
    return manager && !manager->on_object() ? manager : nullptr;
}

objects::GrindPoint* rail_node_named(uint32_t name)
{
    const objects::GrindRailHub* rails = get_rails();
    if (!rails || !rails->nodes()) {
        return nullptr;
    }
    for (int i = 0; i < rails->num(); ++i) {
        if (core::at(rails->node_run(), i).label() == name) {
            return &core::at(rails->node_run(), i);
        }
    }
    return nullptr;
}

// A structure's own links= (not an included structure's) as names.
std::vector<uint32_t> own_links(script::Struct* node)
{
    std::vector<uint32_t> links;
    if (!node) {
        return links;
    }
    for (const script::Component* component = node->head(); component; component = component->next) {
        if (component->name == kLinks &&
            (value_kind(*component) == script::QbValueKind::kArray ||
             value_kind(*component) == script::QbValueKind::kArrayRef) &&
            component->value.array()) {
            links.clear();
            for (uint32_t i = 0; i < component->value.array()->extent(); ++i) {
                links.push_back(component->value.array()->fetch_part(i));
            }
        }
    }
    return links;
}

// A level structure's own links= set (in place when it has one; removed for none).
void set_own_links(script::Struct* node, const std::vector<uint32_t>& links)
{
    const script::Component* own = nullptr;
    for (const script::Component* component = node->head(); component; component = component->next) {
        if (component->name == kLinks &&
            (value_kind(*component) == script::QbValueKind::kArray ||
             value_kind(*component) == script::QbValueKind::kArrayRef) &&
            component->value.array()) {
            own = component;
        }
    }
    if (links.empty()) {
        if (own) {
            node->erase_module(kLinks);
        }
        return;
    }
    if (own) {
        own->value.array()->configure_size_kind(static_cast<uint32_t>(links.size()), script::QbValueKind::kName);
        for (size_t i = 0; i < links.size(); ++i) {
            own->value.array()->set_element(static_cast<uint32_t>(i), core::at(links, i));
        }
        return;
    }
    gsl::owner<script::Array*> array = script::alloc_script_array();
    if (!array) {
        return;
    }
    new (array) script::Array;
    array->configure_size_kind(static_cast<uint32_t>(links.size()), script::QbValueKind::kName);
    for (size_t i = 0; i < links.size(); ++i) {
        array->set_element(static_cast<uint32_t>(i), core::at(links, i));
    }
    node->append_array_ref(kLinks, array);
}

// A structure's own pos= set (the one the game reads: the last).
void write_pos(script::Struct* node, V3 pos)
{
    const script::Component* own = nullptr;
    for (const script::Component* component = node->head(); component; component = component->next) {
        if (component->name == kPos && value_kind(*component) == script::QbValueKind::kVector &&
            component->value.vector()) {
            own = component;
        }
    }
    if (own) {
        own->value.vector()->x() = pos.x;
        own->value.vector()->y() = pos.y;
        own->value.vector()->z() = pos.z;
    } else {
        node->put_vec3(checksums::Name{kPos}, {.x = pos.x, .y = pos.y, .z = pos.z});
    }
}

// A structure's own angles= set (as write_pos).
void write_angles(script::Struct* node, V3 angles)
{
    constexpr uint32_t kAngles = 0x9D2D0915; // angles
    const script::Component* own = nullptr;
    for (const script::Component* component = node->head(); component; component = component->next) {
        if (component->name == kAngles && value_kind(*component) == script::QbValueKind::kVector &&
            component->value.vector()) {
            own = component;
        }
    }
    if (own) {
        own->value.vector()->x() = angles.x;
        own->value.vector()->y() = angles.y;
        own->value.vector()->z() = angles.z;
    } else {
        node->put_vec3(checksums::Name{kAngles}, {.x = angles.x, .y = angles.y, .z = angles.z});
    }
}

uint32_t class_of(script::Struct* node)
{
    uint32_t cls = 0;
    if (node) {
        node->read_hash_value(kClass, &cls, 0);
    }
    return cls;
}

uint32_t rail_node_class()
{
    static const uint32_t kC = name_hash("RailNode");
    return kC;
}
uint32_t climbing_node_class()
{
    static const uint32_t kC = name_hash("ClimbingNode");
    return kC;
}
uint32_t waypoint_class()
{
    static const uint32_t kC = name_hash("Waypoint");
    return kC;
}
uint32_t pedestrian_class()
{
    static const uint32_t kC = name_hash("Pedestrian");
    return kC;
}
uint32_t vehicle_class()
{
    static const uint32_t kC = name_hash("Vehicle");
    return kC;
}

// The terrain checksums the game knows (Terrain_Types' checksum=): only those are looked up (0x004442C0 reads through
// a null entry for an unknown one).
bool known_terrain(uint32_t terrain)
{
    static const uint32_t kChecksumKey = name_hash("checksum");
    script::Array* types = script::fetch_list("Terrain_Types");
    if (!types || types->kind_tag() != script::QbValueKind::kStruct) {
        return false;
    }
    for (uint32_t i = 0; i < types->extent(); ++i) {
        auto* entry = types->struct_at(i);
        uint32_t value = 0;
        if (entry && entry->read_hash_value(kChecksumKey, &value, 0) && value == terrain) {
            return true;
        }
    }
    return false;
}
bool terrain_byte(const std::string& name, uint8_t* value)
{
    const uint32_t terrain = name_hash(name);
    if (!known_terrain(terrain)) {
        return false;
    }
    *value = surface_kind::get_terrain_index(terrain);
    return true;
}

// What the editor has done to the level's own nodes: links= as the level had them (the structure, to know it's still
// the level loaded), the dropped points (and whether their rail node was on), terrains set (and the level's bytes).
struct OriginalLinks {
    script::Struct* node{};
    std::vector<uint32_t> links;
};
std::map<uint32_t, OriginalLinks>& original_links()
{
    static std::map<uint32_t, OriginalLinks> value;
    return value;
}
std::vector<uint32_t>& stored_dropped()
{
    static core::NoDestroy<std::vector<uint32_t>> dropped;
    return dropped.get();
}
std::map<uint32_t, bool>& drop_active()
{
    static std::map<uint32_t, bool> value;
    return value;
}
struct TerrainSet {
    std::string terrain;
    uint8_t original{};
};
std::map<uint32_t, TerrainSet>& terrains_state()
{
    static std::map<uint32_t, TerrainSet> value;
    return value;
}
// NOT ORIGINAL grouping - level_editor_chains.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_chains_state()).
struct LevelEditorChainsState {
    // The rail manager's node arrays replaced by rebuild_rails (freed with the level).
    std::vector<void*> retired_;
    objects::GrindPoint* built_{}; // (the node array rebuild_rails made last)
    // Objects to make again from their nodes (their path changed) once the game has removed them.
    std::vector<uint32_t> remake_;
    // Drawing: the kind, the last point drawn (0: none yet). An armed pick: what for, and from which node.
    Blit draw_ = Blit::kNone;
    uint32_t draw_last_{};
    ChainOp pick_ = ChainOp::kCancel;
    uint32_t pick_from_{};
    // Drawing a ladder: its foot (once the first click found a wall) and its heading.
    bool ladder_have_foot_{};
    V3 ladder_foot_{};
    float ladder_facing_{};
    // The scenery piece the last snapped click hit (0: none) - a new rail's trick-object cluster.
    uint32_t snap_sector_{};
};

LevelEditorChainsState& level_editor_chains_state()
{
    static core::NoDestroy<LevelEditorChainsState> state;
    return state.get();
}

// Added points' fields that aren't in their nodes (by name): a rail's cluster, grindable on flat ground.
struct Extras {
    uint32_t cluster = 0;
    bool grind_flat_ = false;
    float speed = 0.0F; // (a speed-up's rate - apply_speeds)
};
constexpr uint32_t kTriggerScriptKey = 0x2CA8A299; // TriggerScript
// Level rail points given a speed-up (their rates).
std::map<uint32_t, float>& level_speeds()
{
    static std::map<uint32_t, float> value;
    return value;
}
// The speed-ups in place: each node's structure, its own TriggerScript before ours (to put back) and the script that
// counted then (called first by ours); the per-point scripts last defined.
struct SpeedApplied {
    script::Struct* node = nullptr;
    fields::Own original;
    uint32_t original_script_ = 0;
};
std::map<uint32_t, SpeedApplied>& speed_applied()
{
    static std::map<uint32_t, SpeedApplied> value;
    return value;
}
std::vector<uint8_t>& speed_defined()
{
    static core::NoDestroy<std::vector<uint8_t>> speed_defined;
    return speed_defined.get();
}
// The level's own speed-ups (a RailAccelerate in a rail point's TriggerScript): found once for each rail manager
// array (name, rate).
struct LevelSpeedCache {
    const objects::GrindPoint* nodes = nullptr;
    int count = -1;
    std::vector<std::pair<uint32_t, float>> found;
};
LevelSpeedCache& level_speed_cache()
{
    static core::NoDestroy<LevelSpeedCache> level_speed_cache;
    return level_speed_cache.get();
}
std::unordered_map<uint32_t, Extras>& extras_state()
{
    static std::unordered_map<uint32_t, Extras> value;
    return value;
}
// Level ladder points turned: their facing and the rail manager's orientation as the level had it.
struct FacingSet {
    float facing;
    maths::Vec4 original;
};
std::map<uint32_t, FacingSet>& facings_state()
{
    static std::map<uint32_t, FacingSet> value;
    return value;
}
// Level rail points marked grindable on flat ground.
std::vector<uint32_t>& level_flat()
{
    static core::NoDestroy<std::vector<uint32_t>> level_flat;
    return level_flat.get();
}
// The trick objects the editor registered (name -> cluster).
std::map<uint32_t, uint32_t>& trick_state()
{
    static std::map<uint32_t, uint32_t> value;
    return value;
}
// RailClear's override (flat_grindable): the rail manager's nodes whose segment to the next is grindable on flat
// ground, found again when the nodes or the marks change.
struct FlatCache {
    const objects::GrindPoint* nodes = nullptr;
    int count = -1;
    unsigned generation = ~0U;
    std::vector<const objects::GrindPoint*> marked;
};
// NOT ORIGINAL grouping - the flattened chains and their generation (were globals). Made on first use, never destroyed
// (flat_chains()).
struct FlatChains {
    FlatCache flat_cache_{};
    unsigned flat_generation_{};
};

FlatChains& flat_chains()
{
    static core::NoDestroy<FlatChains> state;
    return state.get();
}

std::vector<Point> added_points()
{
    std::vector<Point> points;
    for (const node_create::Spec& spec : node_create::at_hand()) {
        if (!is_chain_class(spec.cls)) {
            continue;
        }
        Point chain_point;
        chain_point.name = spec.name;
        chain_point.cls = spec.cls;
        chain_point.pos = spec.pos;
        chain_point.like = spec.like;
        chain_point.terrain = spec.terrain;
        chain_point.links = spec.links;
        if (same_text(spec.cls, "ClimbingNode")) {
            chain_point.facing = spec.angles.y; // (a ladder's Angles: (0, facing, 0))
            if (same_text(spec.type, "Ledge")) {
                chain_point.type = "Ledge";
                for (const node_create::Prop& prop : spec.extra) {
                    if (prop.type == node_create::Prop::kFlag && same_text(prop.text, "HangLeft")) {
                        chain_point.hang = kHangLeft;
                    }
                    if (prop.type == node_create::Prop::kFlag && same_text(prop.text, "HangRight")) {
                        chain_point.hang = kHangRight;
                    }
                }
            }
        }
        auto extras = extras_state().find(name_hash(spec.name));
        if (extras != extras_state().end()) {
            chain_point.cluster = extras->second.cluster;
            chain_point.grind_flat_ = extras->second.grind_flat_;
            chain_point.speed = extras->second.speed;
        }
        // (a rail point: where the rail manager has it - a sector moved with it takes it along)
        if (const objects::GrindPoint* node = rail_node_named(name_hash(spec.name))) {
            chain_point.pos = {.x = node->at_point().x, .y = node->at_point().y, .z = node->at_point().z};
        }
        points.push_back(chain_point);
    }
    return points;
}

node_create::Spec spec_of(const Point& chain_point)
{
    node_create::Spec spec;
    spec.name = chain_point.name;
    spec.cls = chain_point.cls;
    spec.pos = chain_point.pos;
    spec.like = chain_point.like;
    spec.terrain = chain_point.terrain;
    spec.links = chain_point.links;
    if (same_text(chain_point.cls, "ClimbingNode")) {
        spec.type = "Ladder";
        spec.angles = {.x = 0.0F, .y = chain_point.facing, .z = 0.0F};
        if (is_ledge(chain_point)) {
            // (THUG2's own ledges: Type=Ledge, CreatedAtStart, a side to hang from when the level gives one)
            spec.type = "Ledge";
            if (chain_point.hang != 0) {
                node_create::Prop flag = {.type = node_create::Prop::kFlag, .key = ""};
                flag.text = chain_point.hang == kHangLeft ? "HangLeft" : "HangRight";
                spec.extra.push_back(flag);
            }
        }
    }
    return spec;
}

// A climbing node's orientation (the rail manager's quaternion, as append_vertex makes it from Angles) for a heading.
maths::Vec4 climb_of(float facing)
{
    maths::Mat4 matrix;
    core::at(matrix.row, 0) = {.x = 1.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
    core::at(matrix.row, 1) = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 0.0F};
    core::at(matrix.row, 2) = {.x = 0.0F, .y = 0.0F, .z = 1.0F, .w = 0.0F};
    core::at(matrix.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    const maths::Vec4 angles = {.x = 0.0F, .y = facing, .z = 0.0F, .w = 1.0F};
    const maths::Mat4* built = &matrix.init_from_angles(angles);
    return maths::quat_from_matrix(*built);
}

// The heading of a climbing node's orientation: its at row, flat.
float heading_of_climb(const maths::Vec4& climb)
{
    maths::Mat4 matrix;
    maths::quat_to_matrix(climb, &matrix);
    return std::atan2(core::at(matrix.row, 2).x, core::at(matrix.row, 2).z);
}

// A ladder point's heading now (the rail manager's; 0: not one).
float facing_now(uint32_t name)
{
    const objects::GrindPoint* node = rail_node_named(name);
    return node && ((node->flag_bits() & objects::kRailFlagClimbing) != 0U) ? heading_of_climb(node->climb()) : 0.0F;
}

// A level ladder point's heading as the level has it.
float facing_original(uint32_t name)
{
    auto it = facings_state().find(name);
    return it != facings_state().end() ? heading_of_climb(it->second.original) : facing_now(name);
}

bool same_facing(float a, float b) { return std::fabs(node_edit::wrap_angle(a - b)) < kFacingEpsilon; }

std::vector<uint32_t> level_links_now(uint32_t name) { return own_links(look_up_node_params(name)); }
std::vector<uint32_t> level_links_original(uint32_t name)
{
    auto it = original_links().find(name);
    return it != original_links().end() ? it->second.links : level_links_now(name);
}

void set_level_links(uint32_t name, const std::vector<uint32_t>& links)
{
    script::Struct* node = look_up_node_params(name);
    if (!node || node_create::is_added(name)) {
        return;
    }
    if (!original_links().contains(name)) {
        original_links()[name] = {.node = node, .links = own_links(node)};
    }
    if (own_links(node) != links) {
        set_own_links(node, links);
    }
}

// The rail manager node's box: to its next node, or its own point.
void cuboid(objects::GrindPoint* node)
{
    const maths::Vec4 here = node->at_point();
    const maths::Vec4 there = node->succ() ? node->succ()->at_point() : node->at_point();
    objects::box_of(&here, &there, &node->lower_limit(), &node->upper_limit());
}

void get_remake(uint32_t name)
{
    auto* object = core::down_cast<objects::GameEntity>(objects::find_object(name));
    if (object && ((object->object_flags() & objects::kObjectFlagDead) == 0U)) {
        object->flag_as_dead();
    }
    if (std::ranges::find(level_editor_chains_state().remake_, name) == level_editor_chains_state().remake_.end()) {
        level_editor_chains_state().remake_.push_back(name);
    }
    get_log("level editor: %s's path changed - it is made again from its node", object_edit::caption(name).c_str());
}

// The drawn rails' trick objects registered with the trick object manager (0x007CE500) as add_from_node_array registers
// the level's TrickObject rails (0x005896D0 the cluster, 0x00589080 the node's name mapped to it); those no longer
// wanted (or of another cluster now) taken out of its name map (the manager's object_clusters_).
void register_trick_objects()
{
    objects::TrickSpotHub* manager = objects::trick_spot_hub();
    if (!manager) {
        return;
    }
    std::map<uint32_t, uint32_t> want;
    for (const node_create::Spec& spec : node_create::at_hand()) {
        const uint32_t name = name_hash(spec.name);
        auto extras = extras_state().find(name);
        if (same_text(spec.cls, "RailNode") && extras != extras_state().end() && (extras->second.cluster != 0U) &&
            (rail_node_named(name) != nullptr)) {
            want[name] = extras->second.cluster;
        }
    }
    lists::KeyMap* objects = &manager->object_clusters();
    for (const auto& entry : trick_state()) {
        auto it = want.find(entry.first);
        if (it == want.end() || it->second != entry.second) {
            objects->erase_entry(entry.first);
        }
    }
    for (const auto& entry : want) {
        auto it = trick_state().find(entry.first);
        if (it != trick_state().end() && it->second == entry.second) {
            continue;
        }
        manager->add_cluster(entry.second); // (false: there already)
        manager->add_object_to_cluster(entry.first, entry.second);
    }
    if (debug_enabled() && want != trick_state()) {
        get_log("level editor: %u drawn rail points are trick objects", static_cast<unsigned>(want.size()));
    }
    trick_state() = want;
}

// The rebuilt rail nodes indexed by name and switched on / off: dropped points off (their state noted first),
// shown ones back as they were.
void set_dropped_states(std::span<objects::GrindPoint> nodes, std::unordered_map<uint32_t, int>& index)
{
    const int count = static_cast<int>(nodes.size());
    for (int i = 0; i < count; ++i) {
        objects::GrindPoint* node = &core::at(nodes, i);
        index.emplace(node->label(), i);
        node->set_succ(node->last_value() = nullptr);
        const bool dropped = std::ranges::find(stored_dropped(), node->label()) != stored_dropped().end();
        auto was = drop_active().find(node->label());
        if (dropped) {
            if (was == drop_active().end()) {
                drop_active()[node->label()] = (node->flag_bits() & objects::kRailFlagActive) != 0;
            }
            node->flag_bits() &= ~objects::kRailFlagActive;
        } else if (was != drop_active().end()) {
            node->flag_bits() = was->second ? node->flag_bits() | objects::kRailFlagActive
                                            : node->flag_bits() & ~objects::kRailFlagActive;
            drop_active().erase(was);
        }
    }
}

// The links, as add_from_node_array makes them: a node's next is its first link (or a DefaultLine one), the linked
// node's previous likewise. Dropped points take no part.
void link_rail_nodes(std::span<objects::GrindPoint> nodes, const std::unordered_map<uint32_t, int>& index)
{
    const int count = static_cast<int>(nodes.size());
    auto is_dropped = [&](uint32_t name) {
        return std::ranges::find(stored_dropped(), name) != stored_dropped().end();
    };
    for (int i = 0; i < count; ++i) {
        objects::GrindPoint* node = &core::at(nodes, i);
        if (is_dropped(node->label())) {
            continue;
        }
        for (const uint32_t link : own_links(look_up_node_params(node->label()))) {
            auto it = index.find(link);
            if (it == index.end() || is_dropped(link)) {
                continue;
            }
            objects::GrindPoint* other = &core::at(nodes, it->second);
            if (!node->succ() || ((other->flag_bits() & objects::kRailFlagDefaultLine) != 0U)) {
                node->set_succ(other);
            }
            if (!other->last_value() || ((node->flag_bits() & objects::kRailFlagDefaultLine) != 0U)) {
                other->last_value() = node;
            }
        }
    }
}

} // namespace

void rebuild_rails()
{
    objects::GrindRailHub* rails = get_rails();
    if (!rails) {
        return;
    }
    const std::span<objects::GrindPoint> old(rails->nodes(), rails->nodes() ? static_cast<size_t>(rails->num()) : 0);
    const int old_count = static_cast<int>(old.size());
    // The nodes: the ones it has whose node is still there (a dropped one too - kept switched off, so the editor's
    // moves of it stay), then the overlay's new rail points.
    std::vector<int> kept;
    for (int i = 0; i < old_count; ++i) {
        if (look_up_node_params(core::at(old, i).label())) {
            kept.push_back(i);
        }
    }
    std::vector<std::pair<uint32_t, script::Struct*>> added;
    for (const node_create::Spec& spec : node_create::at_hand()) {
        const uint32_t name = name_hash(spec.name);
        script::Struct* node = look_up_node_params(name);
        const uint32_t cls = class_of(node);
        if (!node || (cls != rail_node_class() && cls != climbing_node_class())) {
            continue;
        }
        bool there = false;
        for (const int index : kept) {
            there = there || core::at(old, index).label() == name;
        }
        if (!there) {
            added.emplace_back(name, node);
        }
    }
    const int count = static_cast<int>(kept.size() + added.size());
    objects::GrindPoint* nodes_block = nullptr;
    if (count != 0) {
        memory::Hub* manager = memory::Hub::get_pull();
        manager->enter_heap_scope(manager->bottom_up_heap());
        nodes_block = static_cast<objects::GrindPoint*>(memory::alloc_block(count * sizeof(objects::GrindPoint)));
        memory::Hub::get_pull()->leave_heap_scope();
        if (!nodes_block) {
            get_log("level editor: no memory for the rail manager's nodes - rails not rebuilt");
            return;
        }
    }
    const std::span<objects::GrindPoint> nodes(nodes_block, static_cast<size_t>(count));
    for (size_t i = 0; i < kept.size(); ++i) {
        core::at(nodes, i) = core::at(old, core::at(kept, i));
    }
    rails->set_nodes(nodes_block);
    rails->set_num(count);
    for (size_t i = 0; i < added.size(); ++i) {
        rails->set_adding(static_cast<int>(kept.size() + i));
        rails->append_vertex(core::at(added, i).first, core::at(added, i).second); // (the game's own: flags, place, terrain)
    }
    rails->set_adding(count);
    // On / off: dropped points off (their state noted first), shown ones back as they were.
    std::unordered_map<uint32_t, int> index;
    set_dropped_states(nodes, index);
    // The links, as add_from_node_array makes them: a node's next is its first link (or a DefaultLine one), the linked
    // node's previous likewise. Dropped points take no part.
    link_rail_nodes(nodes, index);
    for (int i = 0; i < count; ++i) {
        cuboid(&core::at(nodes, i));
    }
    // An added point's terrain from its node (it is made again when its terrain changes, but its rail manager node was
    // kept); terrains the editor set on the level's points (copied nodes keep theirs).
    for (size_t i = 0; i < kept.size(); ++i) {
        objects::GrindPoint* node = &core::at(nodes, i);
        uint32_t terrain = 0;
        script::Struct* params = node_create::is_added(node->label()) ? look_up_node_params(node->label()) : nullptr;
        if (params && ((node->flag_bits() & objects::kRailFlagClimbing) == 0U) &&
            params->read_hash_value(kSurfaceKindKey, &terrain, 0) && known_terrain(terrain)) {
            node->set_surface_type(surface_kind::get_terrain_index(terrain));
        }
    }
    for (const auto& entry : terrains_state()) {
        auto it = index.find(entry.first);
        uint8_t value = 0;
        if (it != index.end() && terrain_byte(entry.second.terrain, &value)) {
            core::at(nodes, it->second).set_surface_type(value);
        }
    }
    // Ladders: an added point's orientation from its node's Angles (turned since it was made), a level point's as the
    // editor turned it.
    for (int i = 0; i < count; ++i) {
        objects::GrindPoint* node = &core::at(nodes, i);
        if ((node->flag_bits() & objects::kRailFlagClimbing) == 0U) {
            continue;
        }
        if (node_create::is_added(node->label())) {
            maths::Vec4 angles;
            level_nodes::read_turn_angles(look_up_node_params(node->label()), &angles);
            node->set_climb(climb_of(angles.y));
        } else if (facings_state().contains(node->label())) {
            node->set_climb(climb_of(facings_state()[node->label()].facing));
        }
    }
    if (!old.empty()) {
        level_editor_chains_state().retired_.push_back(old.data()); // (a skater's grind may point into it: freed with the level)
    }
    level_editor_chains_state().built_ = nodes_block;
    register_trick_objects();
    ++flat_chains().flat_generation_;
    scenery::rails_rebuilt();
    nodes::mark_stale();
    if (debug_enabled()) {
        get_log("level editor: rail manager built again - %d nodes (%u kept, %u new)", count,
                static_cast<unsigned>(kept.size()), static_cast<unsigned>(added.size()));
    }
}

State at_hand()
{
    State state;
    if (!in_level()) {
        return state;
    }
    state.points = added_points();
    for (const auto& entry : original_links()) {
        script::Struct* node = look_up_node_params(entry.first);
        if (node != entry.second.node) {
            continue;
        }
        const std::vector<uint32_t> now = own_links(node);
        if (now != entry.second.links) {
            state.links.push_back({.name = entry.first, .links = now});
        }
    }
    state.dropped = stored_dropped();
    std::ranges::sort(state.dropped);
    for (const auto& entry : terrains_state()) {
        state.terrains.push_back({.name = entry.first, .terrain = entry.second.terrain});
    }
    for (const auto& entry : facings_state()) {
        state.facings.push_back({.name = entry.first, .facing = entry.second.facing});
    }
    state.grind_flat_ = level_flat();
    std::ranges::sort(state.grind_flat_);
    for (const auto& entry : level_speeds()) {
        state.speeds.push_back({.name = entry.first, .rate = entry.second}); // (a map: sorted by name)
    }
    return state;
}

namespace {

// (the points not wanted, or wanted different - other than place and links - removed)
void remove_changed_points(const std::vector<Point>& points, const State& target)
{
    for (const Point& chain_point : points) { // (change_phase's own copy of the points before)
        const Point* want = nullptr;
        for (const Point& target_point : target.points) {
            want = same_text(target_point.name, chain_point.name.c_str()) ? &target_point : want;
        }
        if (!want || want->cls != chain_point.cls || want->like != chain_point.like ||
            want->terrain != chain_point.terrain || !same_text(want->type, chain_point.type.c_str()) ||
            want->hang != chain_point.hang) {
            node_create::Spec removed;
            int at = 0;
            node_create::drop_out(name_hash(chain_point.name), &removed, &at);
        }
    }
}

// (the missing points made: one re-registration)
void make_missing_points(const State& target)
{
    std::vector<node_create::Spec> make;
    const std::vector<Point> remaining = added_points();
    for (const Point& target_point : target.points) {
        bool there = false;
        for (const Point& remaining_point : remaining) {
            there = there || same_text(target_point.name, remaining_point.name.c_str());
        }
        if (!there) {
            make.push_back(spec_of(target_point));
        }
    }
    if (!make.empty()) {
        std::string message;
        node_create::create_all(make, &message);
        if (!message.empty()) {
            get_log("level editor: %s", message.c_str());
        }
    }
}

// (the points' links and places - a ladder's facing)
void set_point_places(const State& target)
{
    for (const Point& target_point : target.points) {
        const uint32_t name = name_hash(target_point.name);
        node_create::set_links(name, target_point.links);
        if (script::Struct* node = look_up_node_params(name)) {
            maths::Vec4 pos;
            level_nodes::fetch_place(node, &pos);
            if (pos.x != target_point.pos.x || pos.y != target_point.pos.y || pos.z != target_point.pos.z) {
                write_pos(node, target_point.pos);
            }
            maths::Vec4 angles;
            level_nodes::read_turn_angles(node, &angles);
            if (same_text(target_point.cls, "ClimbingNode") &&
                (angles.x != 0.0F || angles.y != target_point.facing || angles.z != 0.0F)) {
                write_angles(node, {.x = 0.0F,
                                    .y = target_point.facing,
                                    .z = 0.0F}); // (a ladder turned: rebuild_rails turns its rail node)
            }
        }
    }
}

// (the points' cluster, grind-flat and speed-up)
void set_point_extras(const State& target)
{
    extras_state().clear();
    for (const Point& target_point : target.points) {
        if ((target_point.cluster != 0U) || target_point.grind_flat_ || target_point.speed > 0.0F) {
            extras_state()[name_hash(target_point.name)] = {
                .cluster = target_point.cluster,
                .grind_flat_ = target_point.grind_flat_,
                .speed = target_point.speed > 0.0F ? clamp_speed(target_point.speed) : 0.0F};
        }
    }
}

// (the level rails' speed-ups)
void set_level_speeds(const State& target)
{
    level_speeds().clear();
    for (const SpeedEdit& speed_edit : target.speeds) {
        if (!node_create::is_added(speed_edit.name) && look_up_node_params(speed_edit.name)) {
            level_speeds()[speed_edit.name] = clamp_speed(speed_edit.rate);
        }
    }
}

void set_level_facings(const State& target)
{
    // Level ladders turned (the rail manager's orientation as the level had it kept to put back), level rails marked
    // grindable on flat ground.
    for (auto it = facings_state().begin(); it != facings_state().end();) {
        bool wanted = false;
        for (const FacingEdit& facing_edit : target.facings) {
            wanted = wanted || facing_edit.name == it->first;
        }
        if (!wanted) {
            if (objects::GrindPoint* node = rail_node_named(it->first)) {
                node->set_climb(it->second.original);
            }
            it = facings_state().erase(it);
        } else {
            ++it;
        }
    }
    for (const FacingEdit& facing_edit : target.facings) {
        const objects::GrindPoint* node = rail_node_named(facing_edit.name);
        if (!node || ((node->flag_bits() & objects::kRailFlagClimbing) == 0U) ||
            node_create::is_added(facing_edit.name)) {
            continue;
        }
        if (!facings_state().contains(facing_edit.name)) {
            facings_state()[facing_edit.name] = {.facing = facing_edit.facing, .original = node->climb()};
        }
        facings_state()[facing_edit.name].facing = facing_edit.facing;
    }
}

// (the level rails grindable on flat ground)
void set_level_flat(const State& target)
{
    level_flat().clear();
    for (const uint32_t name : target.grind_flat_) {
        if (!node_create::is_added(name) && look_up_node_params(name)) {
            level_flat().push_back(name);
        }
    }
}

std::vector<uint32_t> set_level_link_edits(const State& target)
{
    // The level's links: as the target has them, else as the level had them.
    std::set<uint32_t> names;
    for (const auto& entry : original_links()) {
        names.insert(entry.first);
    }
    for (const LinkEdit& link_edit : target.links) {
        names.insert(link_edit.name);
    }
    std::vector<uint32_t> paths_changed;
    for (const uint32_t name : names) {
        std::vector<uint32_t> want = level_links_original(name);
        for (const LinkEdit& link_edit : target.links) {
            if (link_edit.name == name) {
                want = link_edit.links;
            }
        }
        if (level_links_now(name) != want) {
            const uint32_t cls = class_of(look_up_node_params(name));
            if (cls == pedestrian_class() || cls == vehicle_class()) {
                paths_changed.push_back(name);
            }
        }
        set_level_links(name, want);
    }
    return paths_changed;
}

void set_dropped(const State& target)
{
    // Dropped points: hidden with phase 3's marker (shown again when no longer dropped).
    std::vector<uint32_t> dropped = target.dropped;
    for (const uint32_t name : std::vector<uint32_t>(stored_dropped())) {
        if (std::ranges::find(dropped, name) == dropped.end()) {
            node_create::conceal(name, false);
        }
    }
    std::vector<uint32_t> kept;
    for (const uint32_t name : dropped) {
        if (node_create::is_added(name) || !look_up_node_params(name)) {
            continue;
        }
        if (node_create::get_hidden(name) || node_create::conceal(name, true)) {
            kept.push_back(name);
        }
    }
    stored_dropped() = kept;
}

void set_level_terrains(const State& target)
{
    // Terrains (level points): put back where no longer set.
    for (auto it = terrains_state().begin(); it != terrains_state().end();) {
        bool wanted = false;
        for (const TerrainEdit& terrain_edit : target.terrains) {
            wanted = wanted || terrain_edit.name == it->first;
        }
        if (!wanted) {
            if (objects::GrindPoint* node = rail_node_named(it->first)) {
                node->set_surface_type(it->second.original);
            }
            it = terrains_state().erase(it);
        } else {
            ++it;
        }
    }
    for (const TerrainEdit& terrain_edit : target.terrains) {
        const objects::GrindPoint* node = rail_node_named(terrain_edit.name);
        uint8_t value = 0;
        if (!node || !terrain_byte(terrain_edit.terrain, &value)) {
            continue;
        }
        if (!terrains_state().contains(terrain_edit.name)) {
            terrains_state()[terrain_edit.name] = {.terrain = terrain_edit.terrain, .original = node->surface_type()};
        }
        terrains_state()[terrain_edit.name].terrain = terrain_edit.terrain;
    }
}

} // namespace

bool change_phase(const State& target)
{
    if (!in_level()) {
        return false;
    }
    const State before = at_hand();
    if (same_state(before, target)) {
        return true; // (nothing to do: the rail manager isn't built again)
    }
    // Points: those not wanted (or wanted different, other than place and links) removed; the missing made (one
    // re-registration); places and links set.
    remove_changed_points(before.points, target);
    make_missing_points(target);
    set_point_places(target);
    set_point_extras(target);
    set_level_speeds(target);
    set_level_facings(target);
    set_level_flat(target);
    const std::vector<uint32_t> paths_changed = set_level_link_edits(target);
    set_dropped(target);
    set_level_terrains(target);
    rebuild_rails();
    // The added points' places in the rail manager (a point made before keeps its node there).
    for (const Point& target_point : target.points) {
        move_added_point(name_hash(target_point.name), target_point.pos);
    }
    for (const uint32_t name : paths_changed) {
        get_remake(name);
    }
    apply_speeds();
    nodes::mark_stale();
    return true;
}

void forget_level()
{
    for (const auto& entry : original_links()) {
        script::Struct* node = look_up_node_params(entry.first);
        if (node && node == entry.second.node) {
            set_own_links(node, entry.second.links);
        }
    }
    original_links().clear();
    stored_dropped().clear();
    drop_active().clear();
    terrains_state().clear();
    level_editor_chains_state().remake_.clear();
    level_editor_chains_state().draw_ = Blit::kNone;
    level_editor_chains_state().draw_last_ = 0;
    level_editor_chains_state().pick_ = ChainOp::kCancel;
    level_editor_chains_state().ladder_have_foot_ = false;
    // (the rail manager and the trick object manager are the level's: they go with it)
    extras_state().clear();
    facings_state().clear();
    level_flat().clear();
    trick_state().clear();
    ++flat_chains().flat_generation_;
    // (the speed-ups' TriggerScripts: the level's nodes put back while its array is the one loaded)
    for (const auto& entry : speed_applied()) {
        script::Struct* node = look_up_node_params(entry.first);
        if (node && node == entry.second.node) {
            fields::put_in(node, kTriggerScriptKey, false, entry.second.original);
        }
    }
    speed_applied().clear();
    level_speeds().clear();
    level_speed_cache() = LevelSpeedCache{};
}

namespace {

// The rail manager's nodes whose segment to the next is grindable on flat ground: an added point marked so, a level
// point marked so, or a node carrying the flag ed_grind_flat (a level baked by apply_layout.ts).
void refresh_flat(objects::GrindRailHub* rails)
{
    if (flat_chains().flat_cache_.nodes == rails->nodes() && flat_chains().flat_cache_.count == rails->num() &&
        flat_chains().flat_cache_.generation == flat_chains().flat_generation_) {
        return;
    }
    flat_chains().flat_cache_.nodes = rails->nodes();
    flat_chains().flat_cache_.count = rails->num();
    flat_chains().flat_cache_.generation = flat_chains().flat_generation_;
    flat_chains().flat_cache_.marked.clear();
    if (!rails->nodes() || !level_nodes::node_manager_instance()) {
        return;
    }
    static const uint32_t kFlag = name_hash("ed_grind_flat");
    for (int i = 0; i < rails->num(); ++i) {
        const objects::GrindPoint* node = &core::at(rails->node_run(), i);
        if (((node->flag_bits() & objects::kRailFlagClimbing) != 0U) || !node->succ()) {
            continue;
        }
        auto extras = extras_state().find(node->label());
        bool marked = (extras != extras_state().end() && extras->second.grind_flat_) ||
                      std::ranges::find(level_flat(), node->label()) != level_flat().end();
        if (!marked) {
            script::Struct* params = look_up_node_params(node->label());
            marked = (params != nullptr) && params->has_bare_flag(kFlag);
        }
        if (marked) {
            flat_chains().flat_cache_.marked.push_back(node);
        }
    }
    if (debug_enabled() && !flat_chains().flat_cache_.marked.empty()) {
        get_log("level editor: %u rail segments grindable on flat ground",
                static_cast<unsigned>(flat_chains().flat_cache_.marked.size()));
    }
}

bool flat_hook(const maths::Vec4* a, const maths::Vec4* b)
{
    return flat_grindable({.x = a->x, .y = a->y, .z = a->z}, {.x = b->x, .y = b->y, .z = b->z});
}

} // namespace

bool is_ledge_node(uint32_t name)
{
    static const uint32_t kLedge = name_hash("Ledge");
    const int index = nodes::get_find(name);
    return index >= 0 && core::at(nodes::get_all(), index).cls == climbing_node_class() && core::at(nodes::get_all(), index).type == kLedge;
}

void apply_speeds()
{
    if (!level_nodes::node_manager_instance() ||
        (speed_applied().empty() && extras_state().empty() && level_speeds().empty())) {
        return;
    }
    // What's wanted (none in a network session: the edited level's changes are offline only).
    std::map<uint32_t, float> want;
    if (!layout::get_refused()) {
        for (const auto& entry : extras_state()) {
            if (entry.second.speed > 0.0F) {
                want[entry.first] = entry.second.speed;
            }
        }
        for (const auto& entry : level_speeds()) {
            want[entry.first] = entry.second;
        }
    }
    // Those no longer wanted put back; a node whose structure was made again (the overlay registered again) forgotten
    // (its new structure has no TriggerScript of ours).
    for (auto it = speed_applied().begin(); it != speed_applied().end();) {
        script::Struct* node = look_up_node_params(it->first);
        if (node != it->second.node) {
            it = speed_applied().erase(it);
            continue;
        }
        if (!want.contains(it->first)) {
            fields::put_in(node, kTriggerScriptKey, false, it->second.original);
            it = speed_applied().erase(it);
            continue;
        }
        ++it;
    }
    std::vector<SpeedScript> scripts;
    for (const auto& w : want) {
        script::Struct* node = look_up_node_params(w.first);
        if (!node) {
            continue;
        }
        const uint32_t ours = name_hash(speed_script_name(w.first));
        auto it = speed_applied().find(w.first);
        if (it == speed_applied().end()) {
            SpeedApplied applied;
            applied.node = node;
            applied.original = fields::own_in(node, kTriggerScriptKey, false);
            node->read_hash_value(kTriggerScriptKey, &applied.original_script_, 0);
            if (applied.original_script_ == ours) {
                applied.original_script_ = 0;
            }
            it = speed_applied().emplace(w.first, applied).first;
        }
        scripts.push_back({.node = w.first, .original = it->second.original_script_, .rate = w.second});
    }
    if (scripts.empty()) {
        return;
    }
    // The scripts: the fixed two (once - they stay defined), the points' when they change.
    static const uint32_t kRailSpeed = name_hash("ed_RailSpeed");
    const script::SymbolTableEntry* fixed = script::resolve_symbol(kRailSpeed);
    if (!fixed || fixed->type() != script::QbValueKind::kScriptBody) {
        std::vector<uint8_t> qb = rail_speed_qb(); // (the loader may relocate a pre-compiled file in place)
        script::load_qb_data("editor\\thug2_decomp_railspeed.qb", qb, 0, 0, 0);
    }
    std::vector<uint8_t> file = speed_qb_file(scripts);
    if (file != speed_defined()) {
        script::load_qb_data("editor\\thug2_decomp_railspeeds.qb", file, 0, 0, 0);
        speed_defined() = file;
        if (debug_enabled()) {
            get_log("level editor: %u rail speed-up scripts defined", static_cast<unsigned>(scripts.size()));
        }
    }
    // Each node's TriggerScript ours (its own component, after any shared one: it counts).
    for (const SpeedScript& speed_script : scripts) {
        script::Struct* node = look_up_node_params(speed_script.node);
        const uint32_t ours = name_hash(speed_script_name(speed_script.node));
        uint32_t now = 0;
        if (!node || (node->read_hash_value(kTriggerScriptKey, &now, 0) && now == ours)) {
            continue;
        }
        fields::Own own;
        own.present = true;
        own.value.kind = fields::Kind::kName;
        own.value.checksum = ours;
        own.index = -1;
        fields::put_in(node, kTriggerScriptKey, false, own);
    }
}

std::vector<SpeedSegment> speed_segments(V3 near, float radius)
{
    std::vector<SpeedSegment> out;
    const objects::GrindRailHub* rails = get_rails();
    if (!rails || !rails->nodes() || !level_nodes::node_manager_instance()) {
        return out;
    }
    // (the level's own RailAccelerate points: found once for each rail manager array)
    if (level_speed_cache().nodes != rails->nodes() || level_speed_cache().count != rails->num()) {
        level_speed_cache().nodes = rails->nodes();
        level_speed_cache().count = rails->num();
        level_speed_cache().found.clear();
        for (int i = 0; i < rails->num(); ++i) {
            const objects::GrindPoint* node = &core::at(rails->node_run(), i);
            script::Struct* params = node->succ() ? look_up_node_params(node->label()) : nullptr;
            uint32_t script = 0;
            if (!params || !params->read_hash_value(kTriggerScriptKey, &script, 0) || (script == 0U)) {
                continue;
            }
            auto applied = speed_applied().find(node->label());
            if (applied != speed_applied().end()) {
                script = applied->second.original_script_; // (the level's own script, under ours)
            }
            const triggers::Calls calls = triggers::script_calls(script);
            if (calls.rail_speed_) {
                level_speed_cache().found.emplace_back(node->label(), calls.rail_rate_);
            }
        }
    }
    float r2 = radius * radius;
    auto add = [&](const objects::GrindPoint* node, float rate, bool level) {
        if (!node || !node->succ() || (node->flag_bits() & objects::kRailFlagClimbing)) {
            return;
        }
        const V3 point_a = {.x = node->at_point().x, .y = node->at_point().y, .z = node->at_point().z};
        const V3 point_b = {
            .x = node->succ()->at_point().x, .y = node->succ()->at_point().y, .z = node->succ()->at_point().z};
        const V3 da = point_a - near;
        const V3 db = point_b - near;
        if (get_dot(da, da) > r2 && get_dot(db, db) > r2) {
            return;
        }
        out.push_back({.a = point_a, .b = point_b, .rate = rate, .level = level});
    };
    for (const auto& entry : speed_applied()) {
        auto extras = extras_state().find(entry.first);
        auto level = level_speeds().find(entry.first);
        float rate = 0.0F;
        if (extras != extras_state().end()) {
            rate = extras->second.speed;
        } else if (level != level_speeds().end()) {
            rate = level->second;
        }
        if (rate > 0.0F) {
            add(rail_node_named(entry.first), rate, false);
        }
    }
    for (const auto& found : level_speed_cache().found) {
        if (!speed_applied().contains(found.first)) {
            add(rail_node_named(found.first), found.second, true);
        }
    }
    return out;
}

bool flat_grindable(V3 a, V3 b)
{
    objects::GrindRailHub* rails = get_rails();
    if (!rails) {
        return false;
    }
    refresh_flat(rails);
    return std::ranges::any_of(flat_chains().flat_cache_.marked, [&](const objects::GrindPoint* node) {
        const objects::GrindPoint* next = node->succ();
        return next != nullptr &&
               same_segment(a, b, {.x = node->at_point().x, .y = node->at_point().y, .z = node->at_point().z},
                            {.x = next->at_point().x, .y = next->at_point().y, .z = next->at_point().z},
                            kSameSegmentTolerance);
    });
}

void tick()
{
    static bool hooked;
    if (!hooked) {
        objects::set_rail_clear_override(&flat_hook); // (RailClear asks it - see the top)
        hooked = true;
    }
    // The rail manager's old node arrays: freed once the level's rails are no longer the ones built here (the game
    // made a new level's - its Cleanup freed ours - or there are none).
    if (!level_editor_chains_state().retired_.empty()) {
        const objects::GrindRailHub* rails = get_rails();
        if (!rails || rails->nodes() != level_editor_chains_state().built_) {
            for (void* nodes : level_editor_chains_state().retired_) {
                memory::free_malloc(nodes);
            }
            level_editor_chains_state().retired_.clear();
            level_editor_chains_state().built_ = nullptr;
        }
    }
    apply_speeds();
    if (level_editor_chains_state().remake_.empty()) {
        return;
    }
    if (!in_level()) {
        level_editor_chains_state().remake_.clear();
        return;
    }
    for (auto it = level_editor_chains_state().remake_.begin(); it != level_editor_chains_state().remake_.end();) {
        if (objects::find_object(*it)) {
            ++it; // (not removed yet: the game deletes dead objects in its update)
            continue;
        }
        if (script::Struct* node = look_up_node_params(*it)) {
            commands::create_from_node(node);
        }
        get_log("level editor: %s made again (its path from its links)", object_edit::caption(*it).c_str());
        it = level_editor_chains_state().remake_.erase(it);
    }
}

bool is_chain_node(uint32_t name)
{
    const int index = nodes::get_find(name);
    if (index < 0) {
        return false;
    }
    const nodes::Family family = core::at(nodes::get_all(), index).family;
    return family == nodes::Family::kRail || family == nodes::Family::kLadder || family == nodes::Family::kRouteWay;
}
bool is_path(uint32_t name)
{
    const int index = nodes::get_find(name);
    return index >= 0 && core::at(nodes::get_all(), index).family == nodes::Family::kRouteWay;
}
bool is_ladder(uint32_t name)
{
    const int index = nodes::get_find(name);
    return index >= 0 && core::at(nodes::get_all(), index).family == nodes::Family::kLadder;
}

bool is_added_point(uint32_t name)
{
    if (!node_create::is_added(name)) {
        return false;
    }
    const int index = nodes::get_find(name);
    return index >= 0 && (core::at(nodes::get_all(), index).family == nodes::Family::kRail ||
                          core::at(nodes::get_all(), index).family == nodes::Family::kLadder ||
                          core::at(nodes::get_all(), index).family == nodes::Family::kRouteWay);
}

bool move_added_point(uint32_t name, V3 pos)
{
    script::Struct* node = node_create::is_added(name) ? look_up_node_params(name) : nullptr;
    if (!node) {
        return false;
    }
    maths::Vec4 now;
    level_nodes::fetch_place(node, &now);
    if (now.x != pos.x || now.y != pos.y || now.z != pos.z) {
        write_pos(node, pos);
    }
    if (objects::GrindPoint* rail = rail_node_named(name)) {
        if (rail->at_point().x != pos.x || rail->at_point().y != pos.y || rail->at_point().z != pos.z) {
            rail->at_point() = {.x = pos.x, .y = pos.y, .z = pos.z, .w = 1.0F};
            const objects::GrindRailHub* rails = get_rails();
            for (int i = 0; i < rails->num(); ++i) {
                if (&core::at(rails->node_run(), i) == rail || core::at(rails->node_run(), i).succ() == rail) {
                    cuboid(&core::at(rails->node_run(), i));
                }
            }
            scenery::rails_rebuilt(); // (the node layer reads the places again)
        }
    }
    const int index = nodes::get_find(name);
    if (index >= 0) {
        nodes::reread(index);
    }
    return true;
}

namespace {

// The chain graph of a family's nodes (as their links= are now): rails (RailNode), paths (Waypoint).
Graph graph_of(nodes::Family family)
{
    Graph graph;
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    for (const nodes::Elem& node : nodes) {
        if (node.family != family || (node.name == 0U) || node.hidden) {
            continue;
        }
        std::vector<uint32_t> links;
        for (const int link : node.links) {
            if (link >= 0 && std::cmp_less(link, nodes.size()) && core::at(nodes, link).family == family &&
                !core::at(nodes, link).hidden) {
                links.push_back(core::at(nodes, link).name);
            }
        }
        graph[node.name] = links;
    }
    return graph;
}

nodes::Family family_of_name(uint32_t name)
{
    const int index = nodes::get_find(name);
    return index >= 0 ? core::at(nodes::get_all(), index).family : nodes::Family::kOther;
}

V3 place_of(uint32_t name)
{
    node_edit::Values values;
    if (node_edit::get_take_in(name, &values)) {
        return values.pos;
    }
    const int index = nodes::get_find(name);
    return index >= 0 ? core::at(nodes::get_all(), index).pos : V3{};
}

// A level node's links in the state: an edit, or none when they're as the level had them.
void set_link_edit(State* state, uint32_t name, const std::vector<uint32_t>& links)
{
    auto it = std::ranges::find_if(state->links, [&](const LinkEdit& link_edit) { return link_edit.name == name; });
    if (links == level_links_original(name)) {
        if (it != state->links.end()) {
            state->links.erase(it);
        }
    } else if (it != state->links.end()) {
        it->links = links;
    } else {
        state->links.push_back({.name = name, .links = links});
        sort_by_name(&state->links);
    }
}

// The graph's links put into the state: an added point's on its point, a level node's as a links edit (none when as
// the level had them).
void put_graph(State* state, const Graph& graph)
{
    for (const auto& entry : graph) {
        bool point = false;
        for (Point& chain_point : state->points) {
            if (name_hash(chain_point.name) == entry.first) {
                chain_point.links = entry.second;
                point = true;
            }
        }
        if (point) {
            continue;
        }
        // (a level node: only the links within the family are in the graph - others it has are kept)
        std::vector<uint32_t> links = entry.second;
        const nodes::Family family = family_of_name(entry.first);
        for (const uint32_t other : level_links_now(entry.first)) {
            if (family_of_name(other) != family && std::ranges::find(links, other) == links.end()) {
                links.push_back(other);
            }
        }
        set_link_edit(state, entry.first, links);
    }
}

// A new point's name: ed_rail01 / ed_path01... free in the level and in the state.
std::string fresh_point_name(nodes::Family family, const State& state, bool ledge = false)
{
    const char* stem = "ed_rail";
    if (family == nodes::Family::kRouteWay) {
        stem = "ed_path";
    } else if (ledge) {
        stem = "ed_ledge";
    } else if (family == nodes::Family::kLadder) {
        stem = "ed_ladder";
    }
    return node_create::fresh_name(stem, [&](const std::string& name) {
        for (const Point& chain_point : state.points) {
            if (same_text(chain_point.name, name.c_str())) {
                return true;
            }
        }
        return node_create::name_taken(name);
    });
}

// A new point drawn on from `from` (0: a new chain of `family`): its class, and the fields it copies.
Point append_rail_vertex(nodes::Family family, uint32_t from, V3 pos, const State& state)
{
    // (a ledge: drawn as one, or drawn on from one)
    const bool ledge = family == nodes::Family::kLadder &&
                       (level_editor_chains_state().draw_ == Blit::kLedge || ((from != 0U) && is_ledge_node(from)));
    Point chain_point;
    chain_point.name = fresh_point_name(family, state, ledge);
    if (family == nodes::Family::kRouteWay) {
        chain_point.cls = "Waypoint";
    } else if (family == nodes::Family::kLadder) {
        chain_point.cls = "ClimbingNode";
    } else {
        chain_point.cls = "RailNode";
    }
    chain_point.pos = pos;
    if (ledge) {
        chain_point.type = "Ledge";
    }
    if (from != 0U) {
        chain_point.like = from;
        bool added = false;
        for (const Point& source_point : state.points) {
            if (name_hash(source_point.name) == from) {
                // (an added point: its own template and terrain - the layout makes them in one go - its cluster and
                // whether it's grindable on flat ground; a ledge's hang side)
                chain_point.like = source_point.like;
                chain_point.terrain = source_point.terrain;
                chain_point.cluster = source_point.cluster;
                chain_point.grind_flat_ = source_point.grind_flat_;
                chain_point.hang = source_point.hang;
                added = true;
            }
        }
        if (!added && family == nodes::Family::kRail) {
            // (a level rail's point: its cluster when it's a trick object, as the level registers it)
            script::Struct* node = look_up_node_params(from);
            static const uint32_t kTrickObject = name_hash("TrickObject");
            static const uint32_t kCluster = name_hash("Cluster");
            if (node && node->has_entry_named(kTrickObject)) {
                chain_point.cluster = from;
                node->read_hash_value(kCluster, &chain_point.cluster, 0);
            }
            chain_point.grind_flat_ = std::ranges::find(state.grind_flat_, from) != state.grind_flat_.end();
        }
    } else if (family == nodes::Family::kRail) {
        // (a new rail: a trick object of the scenery piece the click hit, as the levels name their rails' clusters -
        // else of its own)
        chain_point.cluster = (level_editor_chains_state().snap_sector_ != 0U)
                                  ? level_editor_chains_state().snap_sector_
                                  : name_hash(chain_point.name);
    }
    return chain_point;
}

bool get_apply(const State& state, std::string* message, const char* done)
{
    if (!change_phase(state)) {
        *message = "Not in a level";
        return false;
    }
    *message = done;
    return true;
}

// A ladder point's heading set in the state: an added point's facing, a level point's `facing` edit (none when as the
// level has it).
void set_facing(State* state, uint32_t name, float facing)
{
    for (Point& chain_point : state->points) {
        if (name_hash(chain_point.name) == name) {
            chain_point.facing = facing;
            return;
        }
    }
    auto it =
        std::ranges::find_if(state->facings, [&](const FacingEdit& facing_edit) { return facing_edit.name == name; });
    if (same_facing(facing, facing_original(name))) {
        if (it != state->facings.end()) {
            state->facings.erase(it);
        }
    } else if (it != state->facings.end()) {
        it->facing = facing;
    } else {
        state->facings.push_back({.name = name, .facing = facing});
        sort_by_name(&state->facings);
    }
}

// The operations on a ladder (its two points - or a level's climbing chain - together).
bool ladder_op(ChainOp op, uint32_t name, const nodes::Elem& node, uint32_t arg, std::string* message, uint32_t* select)
{
    if (node.hidden) {
        *message = "Deleted";
        return false;
    }
    State state = at_hand();
    std::vector<uint32_t> points = chain_of(name);
    if (node.added) {
        // (an added ladder: its points by their links too - the rail manager's chain may not have them yet)
        Graph graph;
        for (const Point& chain_point : state.points) {
            if (same_text(chain_point.cls, "ClimbingNode")) {
                graph[name_hash(chain_point.name)] = chain_point.links;
            }
        }
        for (const uint32_t point : get_connected(graph, name)) {
            if (std::ranges::find(points, point) == points.end()) {
                points.push_back(point);
            }
        }
    }
    switch (op) {
    case ChainOp::kDelete: {
        bool level = false;
        for (uint32_t point : points) {
            auto added = std::ranges::find_if(
                state.points, [&](const Point& chain_point) { return name_hash(chain_point.name) == point; });
            if (added != state.points.end()) {
                state.points.erase(added);
                continue;
            }
            level = true;
            if (std::ranges::find(state.dropped, point) == state.dropped.end()) {
                state.dropped.push_back(point);
            }
            std::erase_if(state.facings, [&](const FacingEdit& facing_edit) { return facing_edit.name == point; });
        }
        std::ranges::sort(state.dropped);
        return get_apply(state, message, level ? "Ladder hidden" : "Ladder deleted");
    }
    case ChainOp::kTurn: {
        const int notches = static_cast<int>(arg);
        const float facing =
            node_edit::wrap_angle(facing_now(name) + (static_cast<float>(notches) * (15.0F * 3.14159265F / 180.0F)));
        for (const uint32_t point : points) {
            set_facing(&state, point, facing);
        }
        *select = name;
        std::array<char, kFacingMessageSize> text{};
        text::format_to(text, "Ladder facing %.0f degrees",
                        static_cast<double>(node_edit::get_heading_degrees({.x = 0.0F, .y = facing, .z = 0.0F})));
        const std::string done = text.data();
        return get_apply(state, message, done.c_str());
    }
    case ChainOp::kFacingHome: {
        bool any = false;
        for (uint32_t point : points) {
            auto it = std::ranges::find_if(state.facings,
                                           [&](const FacingEdit& facing_edit) { return facing_edit.name == point; });
            if (it != state.facings.end()) {
                state.facings.erase(it);
                any = true;
            }
        }
        if (!any) {
            return false; // (nothing to put back - no message: Home's own says what it did)
        }
        *select = name;
        return get_apply(state, message, "Ladder facing as the level has it");
    }
    default:
        *message = "Ladder selected";
        return false;
    }
}

} // namespace

std::vector<uint32_t> chain_of(uint32_t name)
{
    const nodes::Family family = family_of_name(name);
    if (family == nodes::Family::kRail || family == nodes::Family::kLadder) {
        const std::vector<uint32_t> rail = scenery::rail_of(name);
        return rail.empty() ? std::vector<uint32_t>{name} : rail;
    }
    if (family == nodes::Family::kRouteWay) {
        return get_connected(graph_of(family), name);
    }
    return {name};
}

namespace {

// What a chain operation works on: the operation, the point (its node, family, whether a ledge's) and its
// argument; where its message and the point to select go.
struct ChainCall {
    ChainOp op{ChainOp::kCancel};
    uint32_t name{};
    uint32_t arg{};
    const nodes::Elem* node{};
    nodes::Family family{};
    bool ledge{};
    std::string* message{};
    uint32_t* select{};
};

// A point put halfway to the next one.
bool insert_point(const ChainCall& call, State& state, Graph& graph)
{
    const uint32_t name = call.name;
    const nodes::Family family = call.family;
    std::string* message = call.message;
    uint32_t* select = call.select;
    const std::vector<uint32_t>& links = graph[name];
    if (links.empty()) {
        *message = "Last point: use Extend";
        return false;
    }
    const V3 point_a = place_of(name);
    const V3 point_b = place_of(links.front());
    const Point chain_point = append_rail_vertex(family, name, (point_a + point_b) * 0.5F, state);
    const uint32_t made = name_hash(chain_point.name);
    state.points.push_back(chain_point);
    insert_after(&graph, name, made);
    put_graph(&state, graph);
    *select = made;
    return get_apply(state, message, "A point put halfway to the next (Ctrl+Z takes it away)");
}

// The point deleted (an added one) or hidden (the level's).
bool delete_point(const ChainCall& call, State& state, Graph& graph)
{
    const uint32_t name = call.name;
    const nodes::Elem& node = *call.node;
    std::string* message = call.message;
    drop_out(&graph, name);
    put_graph(&state, graph);
    if (node.added) {
        std::erase_if(state.points, [&](const Point& chain_point) { return name_hash(chain_point.name) == name; });
    } else {
        state.dropped.push_back(name);
        std::ranges::sort(state.dropped);
        // (its own links stay as the level has them: it takes no part while dropped)
        std::erase_if(state.links, [&](const LinkEdit& link_edit) { return link_edit.name == name; });
    }
    return get_apply(state, message, node.added ? "Point deleted" : "Point hidden");
}

// The point unlinked from its next one: the chain split there.
bool unlink_point(const ChainCall& call, State& state, Graph& graph)
{
    const uint32_t name = call.name;
    std::string* message = call.message;
    uint32_t* select = call.select;
    if (!uncouple(&graph, name)) {
        *message = "It has no next point";
        return false;
    }
    put_graph(&state, graph);
    *select = name;
    return get_apply(state, message, "Unlinked from its next point: the chain is split there");
}

// The rail's terrain (every point of it).
bool set_terrain(const ChainCall& call, State& state)
{
    const uint32_t name = call.name;
    const uint32_t arg = call.arg;
    const nodes::Family family = call.family;
    std::string* message = call.message;
    uint32_t* select = call.select;
    if (family != nodes::Family::kRail) {
        *message = "Only rails have a terrain";
        return false;
    }
    std::string terrain;
    for (const std::string& text : grind_terrain_names()) {
        if (name_hash(text) == arg) {
            terrain = text;
        }
    }
    if (terrain.empty() || !known_terrain(arg)) {
        *message = "Unknown terrain";
        return false;
    }
    for (uint32_t point : chain_of(name)) {
        bool added = false;
        for (Point& chain_point : state.points) {
            if (name_hash(chain_point.name) == point) {
                chain_point.terrain = terrain;
                added = true;
            }
        }
        if (added) {
            continue;
        }
        auto it = std::ranges::find_if(state.terrains,
                                       [&](const TerrainEdit& terrain_edit) { return terrain_edit.name == point; });
        if (it != state.terrains.end()) {
            it->terrain = terrain;
        } else {
            state.terrains.push_back({.name = point, .terrain = terrain});
        }
    }
    sort_by_name(&state.terrains);
    *select = name;
    return get_apply(state, message, ("The rail's terrain is " + terrain + " now").c_str());
}

// Drawing on from an end point.
bool extend_chain(const ChainCall& call, Graph& graph)
{
    const uint32_t name = call.name;
    const nodes::Family family = call.family;
    const bool ledge = call.ledge;
    std::string* message = call.message;
    if (!is_last(graph, name) && !is_first(graph, name)) {
        *message = "Select an end point to extend";
        return false;
    }
    if (family == nodes::Family::kRouteWay) {
        level_editor_chains_state().draw_ = Blit::kRouteWay;
    } else {
        level_editor_chains_state().draw_ = ledge ? Blit::kLedge : Blit::kRail;
    }
    level_editor_chains_state().draw_last_ = name;
    level_editor_chains_state().pick_ = ChainOp::kCancel;
    *message = "Click to add points (Enter: finish)";
    return false;
}

// The drawn points of the rail one trick object (a cluster) or none.
bool set_trick_object(const ChainCall& call, State& state)
{
    const uint32_t name = call.name;
    const uint32_t arg = call.arg;
    const nodes::Family family = call.family;
    std::string* message = call.message;
    uint32_t* select = call.select;
    if (family != nodes::Family::kRail) {
        *message = "Only rails can be trick objects";
        return false;
    }
    // (the drawn points of the rail: one cluster - the one they have, else the scenery piece's or the first's)
    std::vector<uint32_t> rail = chain_of(name);
    uint32_t cluster = 0;
    for (const Point& chain_point : state.points) {
        if (std::ranges::find(rail, name_hash(chain_point.name)) != rail.end() && (cluster == 0U)) {
            cluster = chain_point.cluster;
        }
    }
    bool any = false;
    for (Point& chain_point : state.points) {
        if (std::ranges::find(rail, name_hash(chain_point.name)) == rail.end()) {
            continue;
        }
        if (cluster == 0U) {
            cluster = name_hash(chain_point.name);
        }
        chain_point.cluster = (arg != 0U) ? cluster : 0;
        any = true;
    }
    if (!any) {
        *message = "Original rails keep their trick object setting";
        return false;
    }
    *select = name;
    return get_apply(state, message, (arg != 0U) ? "Trick object on" : "Trick object off");
}

// The rail grindable on flat ground, or not.
bool set_grind_flat(const ChainCall& call, State& state)
{
    const uint32_t name = call.name;
    const uint32_t arg = call.arg;
    const nodes::Elem& node = *call.node;
    const nodes::Family family = call.family;
    std::string* message = call.message;
    uint32_t* select = call.select;
    if (family != nodes::Family::kRail) {
        *message = "Only rails are ground";
        return false;
    }
    for (const uint32_t point : chain_of(name)) {
        bool added = false;
        for (Point& chain_point : state.points) {
            if (name_hash(chain_point.name) == point) {
                chain_point.grind_flat_ = arg != 0;
                added = true;
            }
        }
        if (added) {
            continue;
        }
        auto it = std::ranges::find(state.grind_flat_, point);
        if ((arg != 0U) && it == state.grind_flat_.end()) {
            state.grind_flat_.push_back(point);
        } else if ((arg == 0U) && it != state.grind_flat_.end()) {
            state.grind_flat_.erase(it);
        }
    }
    std::ranges::sort(state.grind_flat_);
    *select = name;
    if (arg != 0U) {
        get_log("level editor: %s's rail grindable on flat ground - RailClear (0x0057C600) passes it as the park "
                "editor's level passes every rail; the game itself has no node flag for this",
                object_edit::caption(name).c_str());
    }
    return get_apply(state, message,
                     (arg != 0U) ? "Grind on flat ground on" : "Ground beside it checked again, as the game does");
}

// A segment's (or the whole rail's) speed-up.
bool set_speed(const ChainCall& call, State& state)
{
    const ChainOp op = call.op;
    const uint32_t name = call.name;
    const uint32_t arg = call.arg;
    const nodes::Family family = call.family;
    std::string* message = call.message;
    uint32_t* select = call.select;
    if (family != nodes::Family::kRail) {
        *message = "Only rails have speed-ups";
        return false;
    }
    const float rate = (arg != 0U) ? clamp_speed(static_cast<float>(arg) / 10000.0F) : 0.0F;
    const std::vector<uint32_t> points = op == ChainOp::kSpeedRail ? chain_of(name) : std::vector<uint32_t>{name};
    int changed = 0;
    for (uint32_t point : points) {
        const objects::GrindPoint* rail = rail_node_named(point);
        if (op == ChainOp::kSpeedRail && (!rail || !rail->succ())) {
            continue; // (a rail's last point has no segment of its own)
        }
        bool added = false;
        for (Point& chain_point : state.points) {
            if (name_hash(chain_point.name) == point) {
                chain_point.speed = rate;
                added = true;
            }
        }
        if (!added) {
            auto it = std::ranges::find_if(state.speeds,
                                           [&](const SpeedEdit& speed_edit) { return speed_edit.name == point; });
            if (rate <= 0.0F && it != state.speeds.end()) {
                state.speeds.erase(it);
            } else if (rate > 0.0F && it != state.speeds.end()) {
                it->rate = rate;
            } else if (rate > 0.0F) {
                state.speeds.push_back({.name = point, .rate = rate});
            }
        }
        ++changed;
    }
    sort_by_name(&state.speeds);
    *select = name;
    std::array<char, kListTextSize> text{};
    if (rate > 0.0F) {
        text::format_to(text, "Speed-up on %d segment%s: %.1f%% per frame", changed, changed == 1 ? "" : "s",
                        rate * 100.0F);
    } else {
        text::format_to(text, "Speed-up removed from %d segment%s", changed, changed == 1 ? "" : "s");
    }
    return get_apply(state, message, text.data());
}

// The side a drawn ledge is hung from.
bool set_hang(const ChainCall& call, State& state)
{
    const uint32_t name = call.name;
    const uint32_t arg = call.arg;
    const bool ledge = call.ledge;
    std::string* message = call.message;
    uint32_t* select = call.select;
    if (!ledge) {
        *message = "Only ledges have a side to hang from";
        return false;
    }
    bool any = false;
    for (const uint32_t point : chain_of(name)) {
        for (Point& chain_point : state.points) {
            if (name_hash(chain_point.name) == point) {
                chain_point.hang = static_cast<int>(arg) == kHangLeft || static_cast<int>(arg) == kHangRight
                                       ? static_cast<int>(arg)
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
    const char* applied = "The game decides the ledge's side";
    if (arg == static_cast<uint32_t>(kHangLeft)) {
        applied = "The ledge is hung from its left side";
    } else if (arg == static_cast<uint32_t>(kHangRight)) {
        applied = "The ledge is hung from its right side";
    }
    return get_apply(state, message, applied);
}

// Linking an end point: the next click picks the end it links to.
bool arm_link(const ChainCall& call, Graph& graph)
{
    const uint32_t name = call.name;
    std::string* message = call.message;
    if (!is_last(graph, name) && !is_first(graph, name)) {
        *message = "Select an end point to link";
        return false;
    }
    level_editor_chains_state().pick_ = ChainOp::kLinkArm;
    level_editor_chains_state().pick_from_ = name;
    *message = "Click the end to link to (Esc: cancel)";
    return false;
}

// A ped's or car's path: the next click picks its first waypoint (AssignArm), or its waypoint links taken away
// (Unassign: its links to anything but waypoints kept). Only peds and cars have paths.
bool assign_path(const ChainCall& call, bool object)
{
    const ChainOp op = call.op;
    const uint32_t name = call.name;
    std::string* message = call.message;
    uint32_t* select = call.select;
    if (!object) {
        *message = "Only peds and cars have paths";
        return false;
    }
    if (op == ChainOp::kAssignArm) {
        level_editor_chains_state().pick_ = ChainOp::kAssignArm;
        level_editor_chains_state().pick_from_ = name;
        *message = "Click the first waypoint (Esc: cancel)";
        return false;
    }
    // (its links to anything but waypoints kept)
    State state = at_hand();
    std::vector<uint32_t> links;
    for (const uint32_t other : level_links_now(name)) {
        if (family_of_name(other) != nodes::Family::kRouteWay) {
            links.push_back(other);
        }
    }
    set_link_edit(&state, name, links);
    *select = name;
    return get_apply(state, message, "No path now - it is made again without one");
}

} // namespace

bool apply_op(ChainOp op, uint32_t name, uint32_t arg, std::string* message, uint32_t* select)
{
    *select = 0;
    message->clear();
    if (op == ChainOp::kCancel) {
        stop_drawing();
        level_editor_chains_state().pick_ = ChainOp::kCancel;
        *message = "Stopped";
        return false;
    }
    if (!in_level()) {
        *message = "Not in a level";
        return false;
    }
    const int index = nodes::get_find(name);
    if (index < 0) {
        return false;
    }
    const nodes::Elem node = core::at(nodes::get_all(), index);
    const nodes::Family family = node.family;
    const bool object = node.cls == pedestrian_class() || node.cls == vehicle_class();
    if (op == ChainOp::kAssignArm || op == ChainOp::kUnassign) {
        return assign_path({.op = op, .name = name, .node = &node, .message = message, .select = select}, object);
    }
    const bool ledge = family == nodes::Family::kLadder && is_ledge_node(name);
    if (family == nodes::Family::kLadder && !ledge) {
        return ladder_op(op, name, node, arg, message, select);
    }
    if (family != nodes::Family::kRail && family != nodes::Family::kRouteWay && !ledge) {
        *message = "Not a rail, ladder or path point";
        return false;
    }
    if (node.hidden) {
        *message = "Deleted";
        return false;
    }
    State state = at_hand();
    Graph graph = graph_of(family);
    const ChainCall call{.op = op,
                         .name = name,
                         .arg = arg,
                         .node = &node,
                         .family = family,
                         .ledge = ledge,
                         .message = message,
                         .select = select};
    switch (op) {
    case ChainOp::kInsert:
        return insert_point(call, state, graph);
    case ChainOp::kDelete:
        return delete_point(call, state, graph);
    case ChainOp::kUnlink:
        return unlink_point(call, state, graph);
    case ChainOp::kTerrain:
        return set_terrain(call, state);
    case ChainOp::kExtend:
        return extend_chain(call, graph);
    case ChainOp::kTrickObject:
        return set_trick_object(call, state);
    case ChainOp::kGrindFlat:
        return set_grind_flat(call, state);
    case ChainOp::kSpeed:
    case ChainOp::kSpeedRail:
        return set_speed(call, state);
    case ChainOp::kHang:
        return set_hang(call, state);
    case ChainOp::kLinkArm:
        return arm_link(call, graph);
    default:
        return false;
    }
}

void start_drawing(Blit kind)
{
    level_editor_chains_state().draw_ = kind;
    level_editor_chains_state().draw_last_ = 0;
    level_editor_chains_state().pick_ = ChainOp::kCancel;
    level_editor_chains_state().ladder_have_foot_ = false;
}
Blit get_drawing() { return level_editor_chains_state().draw_; }
void stop_drawing()
{
    level_editor_chains_state().draw_ = Blit::kNone;
    level_editor_chains_state().draw_last_ = 0;
    level_editor_chains_state().ladder_have_foot_ = false;
}

namespace {

// The family of the chain being drawn (a ledge's points are a ladder's).
nodes::Family drawn_family()
{
    if (level_editor_chains_state().draw_ == Blit::kRouteWay) {
        return nodes::Family::kRouteWay;
    }
    if (level_editor_chains_state().draw_ == Blit::kLedge) {
        return nodes::Family::kLadder;
    }
    return nodes::Family::kRail;
}

} // namespace

bool draw_at(V3 at, uint32_t end, std::string* message, uint32_t* select)
{
    *select = 0;
    if (level_editor_chains_state().draw_ == Blit::kNone || !in_level()) {
        return false;
    }
    if ((end != 0U) && (level_editor_chains_state().draw_last_ == 0U)) {
        // (the first click on an existing end: drawing goes on from it)
        level_editor_chains_state().draw_last_ = end;
        *select = end;
        *message = "Click the next point";
        return false;
    }
    const nodes::Family family = drawn_family();
    State state = at_hand();
    Graph graph = graph_of(family);
    if ((level_editor_chains_state().draw_last_ != 0U) && !graph.contains(level_editor_chains_state().draw_last_)) {
        level_editor_chains_state().draw_last_ = 0; // (it went: a new chain)
    }
    if ((end != 0U) && (level_editor_chains_state().draw_last_ != 0U) &&
        end != level_editor_chains_state().draw_last_) {
        // (onto an existing end: linked, and the drawing done)
        if (!get_link(&graph, level_editor_chains_state().draw_last_, end)) {
            *message = "Can't link: no free end";
            return false;
        }
        put_graph(&state, graph);
        stop_drawing();
        *select = end;
        return get_apply(state, message, "Linked onto that end - drawing finished");
    }
    const Point chain_point = append_rail_vertex(family, level_editor_chains_state().draw_last_, at, state);
    const uint32_t made = name_hash(chain_point.name);
    state.points.push_back(chain_point);
    graph[made] = {};
    if ((level_editor_chains_state().draw_last_ != 0U) &&
        !extend(&graph, level_editor_chains_state().draw_last_, made)) {
        *message = "Not an end point: starting a new chain";
        level_editor_chains_state().draw_last_ = 0;
    }
    put_graph(&state, graph);
    if (!change_phase(state)) {
        *message = "Not in a level";
        return false;
    }
    const bool first = level_editor_chains_state().draw_last_ == 0;
    level_editor_chains_state().draw_last_ = made;
    *select = made;
    if (message->empty()) {
        if (!first) {
            *message = "Point added - Enter or a right click finishes";
        } else if (family == nodes::Family::kRouteWay) {
            *message = "Path started (Enter: finish)";
        } else if (family == nodes::Family::kLadder) {
            *message = "Ledge started (Enter: finish)";
        } else {
            *message = "Rail started (Enter: finish)";
        }
    }
    return true;
}

bool add_rail(const std::vector<V3>& points, uint32_t cluster, std::string* message, uint32_t* select)
{
    *select = 0;
    if (points.size() < 2 || !in_level()) {
        *message = points.size() < 2 ? "A rail needs two points" : "Not in a level";
        return false;
    }
    const Blit drawing = level_editor_chains_state().draw_;
    level_editor_chains_state().draw_ = Blit::kNone; // (append_rail_vertex: a plain rail, whatever is being drawn)
    State state = at_hand();
    Graph graph = graph_of(nodes::Family::kRail);
    uint32_t last = 0;
    uint32_t first = 0;
    for (const V3 at : points) {
        Point chain_point = append_rail_vertex(nodes::Family::kRail, last, at, state);
        const uint32_t made = name_hash(chain_point.name);
        if (last == 0U) {
            chain_point.cluster = (cluster != 0U) ? cluster : made;
        }
        state.points.push_back(chain_point);
        graph[made] = {};
        if (last != 0U) {
            extend(&graph, last, made);
        } else {
            first = made;
        }
        last = made;
    }
    level_editor_chains_state().draw_ = drawing;
    put_graph(&state, graph);
    if (!change_phase(state)) {
        *message = "Not in a level";
        return false;
    }
    *select = first;
    return true;
}

namespace {

// The game's collision ray (as level_editor.cpp's: a ProbeRay, 0x00417760 / 0x00417710; the hit's normal at +0x24, its
// point at +0x34, the collision object hit at +0x54 and its checksum - the scenery piece's - at +0x58, as
// level_editor_scenery.cpp's cast_static reads them).
bool cast_ray(V3 from, V3 to, SurfaceOut surface, uint32_t* sector = nullptr)
{
    auto* const hit = surface.point;
    auto* const normal = surface.normal;
    render::ProbeRay ray_storage;
    render::ProbeRay* ray = &ray_storage;
    const maths::Vec4 start = {.x = from.x, .y = from.y, .z = from.z, .w = 1.0F};
    const maths::Vec4 end = {.x = to.x, .y = to.y, .z = to.z, .w = 1.0F};
    if (ray->get_collision_along(&start, &end, false) == 0) {
        return false;
    }
    const auto& hit_normal = ray->hit().normal;
    const auto& hit_point = ray->hit().point;
    *normal = {.x = hit_normal[0], .y = hit_normal[1], .z = hit_normal[2]};
    *hit = {.x = hit_point[0], .y = hit_point[1], .z = hit_point[2]};
    if (sector) {
        *sector = ray->hit().object ? ray->hit().object_checksum_ : 0;
    }
    return true;
}

// The ground's height below a point (within 70).
bool ground_below(V3 at, float* ground_y)
{
    V3 hit_point{};
    V3 normal{};
    if (!cast_ray(at, {.x = at.x, .y = at.y - kDropProbeDepth, .z = at.z}, {.point = &hit_point, .normal = &normal})) {
        return false;
    }
    *ground_y = hit_point.y;
    return true;
}

} // namespace

bool ladder_click(V3 from, V3 dir, float max_t, std::string* message, uint32_t* select)
{
    *select = 0;
    if (level_editor_chains_state().draw_ != Blit::kLadder || !in_level()) {
        return false;
    }
    if (!level_editor_chains_state().ladder_have_foot_) {
        V3 hit{};
        V3 normal{};
        if (!cast_ray(from, from + dir * max_t, {.point = &hit, .normal = &normal})) {
            *message = "Click on a wall";
            return false;
        }
        V3 foot{};
        if (!ladder_foot(hit, normal, kLadderOut, &ground_below, &foot)) {
            *message = "Click a wall to place the ladder's foot";
            return false;
        }
        level_editor_chains_state().ladder_foot_ = foot;
        level_editor_chains_state().ladder_facing_ = facing_into_wall(normal);
        level_editor_chains_state().ladder_have_foot_ = true;
        *message = "Click to place the top";
        return false;
    }
    V3 top{};
    if (!ladder_top(level_editor_chains_state().ladder_foot_, from, dir, &top)) {
        *message = "Top must be at least 24 units above the foot";
        return false;
    }
    // The ladder: its foot linked to its top, both facing into the wall (as THUG2's own ladders).
    State state = at_hand();
    Point foot = append_rail_vertex(nodes::Family::kLadder, 0, level_editor_chains_state().ladder_foot_, state);
    foot.facing = level_editor_chains_state().ladder_facing_;
    state.points.push_back(foot);
    Point upper = append_rail_vertex(nodes::Family::kLadder, 0, top, state);
    upper.facing = level_editor_chains_state().ladder_facing_;
    state.points.back().links = {name_hash(upper.name)};
    state.points.push_back(upper);
    if (!change_phase(state)) {
        *message = "Not in a level";
        return false;
    }
    stop_drawing();
    *select = name_hash(foot.name);
    std::array<char, kMessageTextSize> text{};
    text::format_to(text, "Ladder added (%.0f high)", static_cast<double>(top.y - level_editor_chains_state().ladder_foot_.y));
    *message = text.data();
    get_log("level editor: added %s / %s (a ladder, facing %.0f degrees)", foot.name.c_str(), upper.name.c_str(),
            static_cast<double>(node_edit::get_heading_degrees(
                {.x = 0.0F, .y = level_editor_chains_state().ladder_facing_, .z = 0.0F})));
    return true;
}

bool magnet_point(V3 from, V3 dir, float max_t, bool free, V3* point, uint32_t* end)
{
    *end = 0;
    V3 hit{};
    V3 normal{};
    if (!cast_ray(from, from + dir * max_t, {.point = &hit, .normal = &normal},
                  &level_editor_chains_state().snap_sector_)) {
        return false;
    }
    *point = hit;
    if (!free) {
        bool snapped = false;
        *point = snap_to_edge(
            hit, normal, kEdgeSnapReach,
            [](V3 at, float* ground_y) {
                V3 hit_point{};
                V3 ray_normal{};
                if (!cast_ray(at, {.x = at.x, .y = at.y - kEdgeProbeDepth, .z = at.z},
                              {.point = &hit_point, .normal = &ray_normal})) {
                    return false;
                }
                *ground_y = hit_point.y;
                return true;
            },
            &snapped);
    }
    // A free end of a chain of the kind drawn, within 24: taken as it is.
    if (level_editor_chains_state().draw_ != Blit::kNone) {
        const nodes::Family family = drawn_family();
        const Graph graph = graph_of(family);
        float best = kJoinReach * kJoinReach;
        for (const auto& entry : graph) {
            if (entry.first == level_editor_chains_state().draw_last_ ||
                (!is_last(graph, entry.first) && !is_first(graph, entry.first))) {
                continue;
            }
            if (level_editor_chains_state().draw_ == Blit::kLedge && !is_ledge_node(entry.first)) {
                continue; // (a ledge links onto ledges, not ladders)
            }
            const V3 at = place_of(entry.first);
            const V3 delta = at - *point;
            const float distance = get_dot(delta, delta);
            if (distance < best) {
                best = distance;
                *end = entry.first;
            }
        }
        if (*end != 0U) {
            *point = place_of(*end);
        }
    }
    return true;
}

bool wants_pick() { return level_editor_chains_state().pick_ == ChainOp::kLinkArm || level_editor_chains_state().pick_ == ChainOp::kAssignArm; }

bool get_picked(uint32_t name, std::string* message, uint32_t* select)
{
    *select = 0;
    const ChainOp pick = level_editor_chains_state().pick_;
    level_editor_chains_state().pick_ = ChainOp::kCancel;
    const uint32_t from = level_editor_chains_state().pick_from_;
    if (pick == ChainOp::kLinkArm) {
        const nodes::Family family = family_of_name(from);
        if (family_of_name(name) != family) {
            *message = "Can only link chains of the same kind";
            return false;
        }
        State state = at_hand();
        Graph graph = graph_of(family);
        if (!get_link(&graph, from, name)) {
            *message = "Can't link: no free end";
            return false;
        }
        put_graph(&state, graph);
        *select = from;
        return get_apply(state, message, "Linked");
    }
    if (pick == ChainOp::kAssignArm) {
        if (family_of_name(name) != nodes::Family::kRouteWay) {
            *message = "Click a path's waypoint";
            return false;
        }
        State state = at_hand();
        std::vector<uint32_t> links = {name};
        for (const uint32_t other : level_links_now(from)) {
            if (family_of_name(other) != nodes::Family::kRouteWay && other != name) {
                links.push_back(other);
            }
        }
        set_link_edit(&state, from, links);
        *select = from;
        return get_apply(state, message, "Path assigned");
    }
    return false;
}

const char* get_waiting()
{
    if (level_editor_chains_state().pick_ == ChainOp::kLinkArm) {
        return "Click the end to link to (Esc: cancel)";
    }
    if (level_editor_chains_state().pick_ == ChainOp::kAssignArm) {
        return "Click the first waypoint (Esc: cancel)";
    }
    if (level_editor_chains_state().draw_ == Blit::kRail) {
        return (level_editor_chains_state().draw_last_ != 0U) ? "Rail: click the next point (Enter: finish)"
                                                              : "Rail: click the first point";
    }
    if (level_editor_chains_state().draw_ == Blit::kRouteWay) {
        return (level_editor_chains_state().draw_last_ != 0U) ? "Path: click the next waypoint (Enter: finish)"
                                                              : "Path: click the first waypoint";
    }
    if (level_editor_chains_state().draw_ == Blit::kLedge) {
        return (level_editor_chains_state().draw_last_ != 0U) ? "Ledge: click the next point (Enter: finish)"
                                                              : "Ledge: click the first point on an edge";
    }
    if (level_editor_chains_state().draw_ == Blit::kLadder) {
        return level_editor_chains_state().ladder_have_foot_ ? "Ladder: click to place the top" : "Ladder: click a wall to place the foot";
    }
    return nullptr;
}

namespace {

// A ledge's hang side: an added point's own, else the node's HangLeft / HangRight.
void read_ledge_hang(const nodes::Elem& node, uint32_t name, View& view)
{
    if (node.added) {
        for (const Point& chain_point : added_points()) {
            if (name_hash(chain_point.name) == name) {
                view.hang = chain_point.hang;
            }
        }
    } else if (script::Struct* params = look_up_node_params(name)) {
        static const uint32_t kLeft = name_hash("HangLeft");
        static const uint32_t kRight = name_hash("HangRight");
        if (params->has_bare_flag(kLeft)) {
            view.hang = kHangLeft;
        } else if (params->has_bare_flag(kRight)) {
            view.hang = kHangRight;
        } else {
            view.hang = 0;
        }
    }
}

// A rail's settings: its speed-up (the editor's, the level's), its own script's speed, its trick-object cluster,
// flat grinding and terrain.
void read_rail_view(const nodes::Elem& node, uint32_t name, View& view)
{
    auto speed = extras_state().find(name);
    auto level_speed = level_speeds().find(name);
    if (speed != extras_state().end()) {
        view.speed = speed->second.speed;
    } else if (level_speed != level_speeds().end()) {
        view.speed = level_speed->second;
    } else {
        view.speed = 0.0F;
    }
    script::Struct* own = look_up_node_params(name);
    uint32_t script = 0;
    auto applied = speed_applied().find(name);
    if (applied != speed_applied().end()) {
        script = applied->second.original_script_;
    } else if (own) {
        own->read_hash_value(kTriggerScriptKey, &script, 0);
    }
    if (script != 0U) {
        const triggers::Calls calls = triggers::script_calls(script);
        view.level_speed_ = calls.rail_speed_;
        view.level_rate_ = calls.rail_rate_;
    }
    auto extras = extras_state().find(name);
    if (node.added) {
        view.can_trick_object_ = true;
        view.trick_object_ = extras != extras_state().end() && (extras->second.cluster != 0U);
        if (view.trick_object_) {
            view.cluster = object_edit::caption(extras->second.cluster);
        }
        view.grind_flat_ = extras != extras_state().end() && extras->second.grind_flat_;
    } else {
        view.trick_object_ = node.trick_object_;
        static const uint32_t kCluster = name_hash("Cluster");
        static const uint32_t kFlag = name_hash("ed_grind_flat");
        script::Struct* params = look_up_node_params(name);
        uint32_t cluster = name;
        if (params) {
            params->read_hash_value(kCluster, &cluster, 0);
        }
        if (view.trick_object_) {
            view.cluster = object_edit::caption(cluster);
        }
        view.grind_flat_ = std::ranges::find(level_flat(), name) != level_flat().end() ||
                           ((params != nullptr) && params->has_bare_flag(kFlag));
    }
    if (const objects::GrindPoint* rail = rail_node_named(name)) {
        for (size_t i = 0; i < view.terrains.size(); ++i) {
            uint8_t value = 0;
            if (terrain_byte(core::at(view.terrains, i), &value) && value == rail->surface_type()) {
                view.terrain = static_cast<int>(i);
                break;
            }
        }
    }
}

} // namespace

View view_of(uint32_t name)
{
    View view;
    view.waiting = get_waiting();
    const int index = nodes::get_find(name);
    if (index < 0 || !in_level()) {
        return view;
    }
    const nodes::Elem& node = core::at(nodes::get_all(), index);
    view.terrains = grind_terrain_names();
    if (node.cls == pedestrian_class() || node.cls == vehicle_class()) {
        view.show = view.object = true;
        for (const uint32_t link : level_links_now(name)) {
            if (family_of_name(link) == nodes::Family::kRouteWay) {
                view.path_start_ = object_edit::caption(link);
            }
        }
        return view;
    }
    view.rail = node.family == nodes::Family::kRail;
    view.ledge = node.family == nodes::Family::kLadder && is_ledge_node(name);
    view.ladder = node.family == nodes::Family::kLadder && !view.ledge;
    view.path = node.family == nodes::Family::kRouteWay;
    if (!view.rail && !view.ladder && !view.path && !view.ledge) {
        return view;
    }
    view.show = true;
    view.added = node.added;
    view.points = static_cast<int>(chain_of(name).size());
    if (node.hidden) {
        view.why = "Deleted";
        return view;
    }
    if (view.ladder) {
        view.can_turn_ = view.can_delete_ = true;
        view.facing_degrees_ = node_edit::get_heading_degrees({.x = 0.0F, .y = facing_now(name), .z = 0.0F});
        view.turned = !node.added && facings_state().contains(name);
        return view;
    }
    Graph graph = graph_of(node.family);
    const std::vector<uint32_t>& links = graph[name];
    std::vector<uint32_t> before = predecessors(graph, name);
    if (!links.empty()) {
        view.next = object_edit::caption(links.front());
    }
    if (!before.empty()) {
        view.prev = object_edit::caption(before.front());
    }
    view.can_insert_ = !links.empty();
    view.can_unlink_ = !links.empty();
    view.can_extend_ = is_last(graph, name) || is_first(graph, name);
    view.can_link_ = view.can_extend_;
    view.can_delete_ = true;
    if (view.ledge) {
        read_ledge_hang(node, name, view);
    }
    if (view.rail) {
        read_rail_view(node, name, view);
    }
    return view;
}

} // namespace frontend::level_editor::chains
