// NOT ORIGINAL - the level editor's chain records: the text lines a chain's edits are saved as, how they are read
// back, and the speed scripts (ours). See level_editor_chains.h.

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
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <initializer_list>
#include <vector>
#include <array>
#include "editor/level_editor.h"
#include "base/core/checksums.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>
#include "base/core/format.h"
#include "engine/script/qb.h"
#include "editor/level_editor_chains_internal.h"

namespace frontend::level_editor::chains {
namespace {

constexpr std::size_t kNumberTextSize = 16;
constexpr std::size_t kChecksumDigits = 8;   // hex digits of a checksum
constexpr std::size_t kMaxNameLength = 63;   // the longest chain name
constexpr std::size_t kPositionTextSize = 96;
constexpr std::size_t kFacingTextSize = 48;
constexpr std::size_t kSpeedTextSize = 48;
constexpr std::size_t kRateTextSize = 48;
constexpr std::size_t kScriptNameSize = 32; // "ed_railspeed_" and eight digits

constexpr int kBitsPerByte = 8;

} // namespace
namespace internal {

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

} // namespace internal
namespace {

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

} // namespace frontend::level_editor::chains
