// NOT ORIGINAL - the level editor's layout text: how a layout is written as lines and read back, record by record
// (ours). See level_editor_layout.h.

#include "editor/level_editor_layout.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ios>
#include <iterator>
#include <span>
#include <sstream>
#include <string>
#include <vector>
#include <array>
#include "editor/level_editor_cameras.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_clusters.h"
#include "editor/level_editor_entities.h"
#include "editor/level_editor_faces.h"
#include "editor/level_editor_fields.h"
#include "editor/level_editor_goals.h"
#include "editor/level_editor_level_props.h"
#include "editor/level_editor_links.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_notes.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_triggers.h"
#include "editor/level_editor_walls.h"
#include "editor/level_editor.h"
#include "editor/level_editor_checks.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_scale.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>
#include "base/core/format.h"

namespace frontend::level_editor::layout {
namespace {

constexpr std::size_t kTripleLineSize = 96; // one "key=x,y,z" line
constexpr std::size_t kNumberLineSize = 32; // one number
constexpr std::size_t kHexLineSize = 16; // one 8-digit hex word
constexpr std::size_t kMaxHexDigits = 8; // the digits of a 32-bit hex word
constexpr float kColourChannelMax = 255.0F; // the largest colour channel
constexpr std::size_t kLightLineSize = 96; // a light's brightness / inner / outer line
constexpr std::size_t kRadiusLineSize = 32; // a radius line
constexpr std::size_t kTurnPrefixLength = 5;  // "turn="
constexpr std::size_t kTiltPrefixLength = 5;  // "tilt="
constexpr double kDegreesPerRadian = 57.295779513082321;

constexpr auto kHeader = std::to_array("thug2-decomp layout "); // (and the version)

// The header's version (0: not a layout's header).
int header_version(const std::string& line)
{
    if (!line.starts_with(kHeader.data())) {
        return 0;
    }
    std::istringstream in(line.substr(sizeof(kHeader) - 1));
    int version = 0;
    std::string rest;
    if (!(in >> version) || version < 1 || (in >> rest)) {
        return 0;
    }
    return version;
}

// "x,y,z" (three numbers, commas between).
bool read_triple(const std::string& text, std::span<float, 3> out)
{
    const char* at = text.c_str();
    for (int i = 0; i < 3; ++i) {
        char* end = nullptr;
        core::at(out, i) = std::strtof(at, &end);
        if (end == at || !std::isfinite(core::at(out, i))) {
            return false;
        }
        at = end;
        if (i < 2) {
            if (*at != ',') {
                return false;
            }
            at = std::next(at);
        }
    }
    return *at == '\0';
}

void triple(std::ostringstream& out, const char* key, std::span<const float, 3> value)
{
    std::array<char, kTripleLineSize> text{};
    text::format_to(text, " %s=%.9g,%.9g,%.9g", key, core::at(value, 0), core::at(value, 1), core::at(value, 2));
    out << text.data();
}

void get_number(std::ostringstream& out, float value)
{
    std::array<char, kNumberLineSize> text{};
    text::format_to(text, " %.9g", value);
    out << text.data();
}

void get_hex(std::ostringstream& out, uint32_t value)
{
    std::array<char, kHexLineSize> text{};
    text::format_to(text, " %08X", value);
    out << text.data();
}

void get_placed(std::ostringstream& out, const Placement& placement)
{
    out << ' ' << placement.quarters;
    get_number(out, placement.offset.x);
    get_number(out, placement.offset.y);
    get_number(out, placement.offset.z);
}

void get_comment(std::ostringstream& out, uint32_t checksum)
{
    if (const char* name = object_edit::get_name_of(checksum)) {
        out << "  # " << name;
    }
    out << '\n';
}

bool read_hex(std::istringstream& in, uint32_t* value)
{
    std::string word;
    if (!(in >> word) || word.empty() || word.size() > kMaxHexDigits) {
        return false;
    }
    char* end = nullptr;
    *value = static_cast<uint32_t>(std::strtoul(word.c_str(), &end, kHexBase));
    return (end != nullptr) && *end == '\0';
}

bool read_placed(std::istringstream& in, Placement* placement)
{
    return static_cast<bool>(in >> placement->quarters >> placement->offset.x >> placement->offset.y >>
                             placement->offset.z) &&
           placement->quarters >= 0 && placement->quarters <= 3;
}

// The key=value fields after a sector / rail / clone / object / copy record's numbers: scale= (level_editor_scale.h);
// others (a later version's) skipped. False: a bad scale.
bool read_scale_fields(std::istringstream& in, V3* scale)
{
    std::string field;
    while (in >> field) {
        if (field.starts_with("scale=")) {
            if (!scale_tool::read_token(field, scale)) {
                return false;
            }
        }
    }
    return true;
}
// A placement's twist and tilt after its numbers: " turn=<degrees>" (any angle beyond its quarter turns) and
// " tilt=<x degrees>,<z degrees>" (its pitch and roll - the Euler X / Z of R = Ry Rx Rz, level_editor.h's Placement);
// each only when not none. An older reader skips tilt= (and places the piece untilted).
std::string twist_token(const Placement& placement)
{
    std::string tokens;
    std::array<char, kNumberLineSize> text{};
    if (placement.twist != 0.0F) {
        text::format_to(text, " turn=%.9g", static_cast<double>(placement.twist) * kDegreesPerRadian);
        tokens += text.data();
    }
    if (is_tilted(placement)) {
        std::array<char, kTripleLineSize> tilt{};
        text::format_to(tilt, " tilt=%.9g,%.9g", static_cast<double>(placement.tilt_x) * kDegreesPerRadian,
                        static_cast<double>(placement.tilt_z) * kDegreesPerRadian);
        tokens += tilt.data();
    }
    return tokens;
}

// A "tilt=<x>,<z>" field's two angles in radians; false when malformed.
bool read_tilt(const std::string& field, float* tilt_x, float* tilt_z)
{
    const char* at = std::next(field.c_str(), kTiltPrefixLength);
    char* end = nullptr;
    const double x = std::strtod(at, &end);
    if (end == at || *end != ',' || !std::isfinite(x)) {
        return false;
    }
    const char* second = std::next(end);
    const double z = std::strtod(second, &end);
    if (end == second || *end != '\0' || !std::isfinite(z)) {
        return false;
    }
    *tilt_x = static_cast<float>(x / kDegreesPerRadian);
    *tilt_z = static_cast<float>(z / kDegreesPerRadian);
    return true;
}

bool read_placed_fields(std::istringstream& in, Placement* placement)
{
    std::string field;
    while (in >> field) {
        if (field.starts_with("scale=")) {
            if (!scale_tool::read_token(field, &placement->scale)) {
                return false;
            }
        } else if (field.starts_with("turn=")) {
            char* end = nullptr;
            const char* at = std::next(field.c_str(), kTurnPrefixLength);
            const double degrees = std::strtod(at, &end);
            if (end == at || *end != '\0' || !std::isfinite(degrees)) {
                return false;
            }
            placement->twist += static_cast<float>(degrees / kDegreesPerRadian);
            normalise_twist(placement);
        } else if (field.starts_with("tilt=")) {
            if (!read_tilt(field, &placement->tilt_x, &placement->tilt_z)) {
                return false;
            }
        }
    }
    return true;
}

// A light's color=r,g,b: whole numbers from 0 to 255; false if one isn't.
bool read_colour(const std::array<float, 3>& v, node_create::Spec* spec)
{
    for (int i = 0; i < 3; ++i) {
        if (core::at(v, i) < 0.0F || core::at(v, i) > kColourChannelMax ||
            core::at(v, i) != static_cast<float>(static_cast<int>(core::at(v, i)))) {
            return false;
        }
        core::at(spec->colour, i) = static_cast<int>(core::at(v, i));
    }
    return true;
}

// A `node` record's fields (after "node"): name, class, then key=value (unknown keys skipped; a repeated one, a bad
// value or a missing place refuse it).
bool read_node(std::istringstream& in, node_create::Spec* spec)
{
    if (!(in >> spec->name >> spec->cls) || !node_create::valid_name(spec->name) ||
        !node_create::valid_name(spec->cls)) {
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
        std::array<float, 3> v{};
        char* end = nullptr;
        if (key == "type") {
            if (!node_create::valid_name(value)) {
                return false;
            }
            spec->type = value;
        } else if (key == "pos" || key == "angles") {
            if (!read_triple(value, v)) {
                return false;
            }
            (key == "pos" ? spec->pos : spec->angles) = {.x = core::at(v, 0), .y = core::at(v, 1), .z = core::at(v, 2)};
        } else if (key == "types") {
            std::istringstream list(value);
            std::string type;
            while (std::getline(list, type, ',')) {
                if (!node_create::valid_name(type)) {
                    return false;
                }
                spec->types.push_back(type);
            }
        } else if (key == "color") {
            if (!read_triple(value, v)) {
                return false;
            }
            if (!read_colour(v, spec)) {
                return false;
            }
        } else if (key == "brightness" || key == "inner" || key == "outer" || key == "radius") {
            const float number = std::strtof(value.c_str(), &end);
            if (value.empty() || *end != '\0' || !std::isfinite(number)) {
                return false;
            }
            if (key == "brightness") {
                spec->brightness = number;
            } else if (key == "inner") {
                spec->inner = number;
            } else if (key == "outer") {
                spec->outer = number;
            } else {
                spec->radius = number;
            }
        }
    }
    return std::ranges::find(seen, "pos") != seen.end();
}

// A node the editor added: its name, class, type, place, restart types, a light's colour and radii, a radius.
void write_added_node(std::ostringstream& out, const node_create::Spec& spec)
{
    out << "node " << spec.name << ' ' << spec.cls;
    if (!spec.type.empty()) {
        out << " type=" << spec.type;
    }
    const std::array<float, 3> pos = {{spec.pos.x, spec.pos.y, spec.pos.z}};
    const std::array<float, 3> angles = {{spec.angles.x, spec.angles.y, spec.angles.z}};
    triple(out, "pos", pos);
    triple(out, "angles", angles);
    if (!spec.types.empty()) {
        out << " types=";
        for (size_t i = 0; i < spec.types.size(); ++i) {
            out << ((i != 0U) ? "," : "") << core::at(spec.types, i);
        }
    }
    node_create::Kind kind{};
    if (node_create::kind_of(spec, &kind) && kind == node_create::Kind::kLight) {
        out << " color=" << core::at(spec.colour, 0) << ',' << core::at(spec.colour, 1) << ','
            << core::at(spec.colour, 2);
        std::array<char, kLightLineSize> text{};
        text::format_to(text, " brightness=%.9g inner=%.9g outer=%.9g", spec.brightness, spec.inner, spec.outer);
        out << text.data();
    }
    if (spec.radius != 0.0F) {
        std::array<char, kRadiusLineSize> text{};
        text::format_to(text, " radius=%.9g", spec.radius);
        out << text.data();
    }
    out << '\n';
}

// A node's edits: its place, angles and fields.
void write_set_record(std::ostringstream& out, const SetRecord& node_record)
{
    out << "set";
    get_hex(out, node_record.name);
    if (node_record.has_pos_) {
        triple(out, "pos", node_record.pos);
    }
    if (node_record.has_angles_) {
        triple(out, "angles", node_record.angles);
    }
    for (const fields::Edit& edit : node_record.fields) {
        out << ' ' << fields::get_token(edit, fields::get_name_of);
    }
    get_comment(out, node_record.name);
}

} // namespace

std::string emit_out(const FileLoad& file)
{
    std::ostringstream out;
    out << kHeader.data() << kVersion << '\n';
    out << "scene";
    get_hex(out, file.scene);
    get_comment(out, file.scene);
    out << level_props::write_record(file.level);
    for (const auto& sector : file.sectors) {
        out << "sector";
        get_hex(out, sector.checksum);
        get_placed(out, sector.placement);
        out << ' ' << (sector.hidden ? 1 : 0) << scale_tool::get_token(sector.placement.scale)
            << twist_token(sector.placement);
        get_comment(out, sector.checksum);
    }
    for (const auto& rail : file.rails) {
        out << "rail";
        get_hex(out, rail.name);
        get_placed(out, rail.placement);
        out << ' ' << (rail.hidden ? 1 : 0) << scale_tool::get_token(rail.placement.scale)
            << twist_token(rail.placement);
        get_comment(out, rail.name);
    }
    for (const auto& clone : file.clones) {
        out << "clone";
        get_hex(out, clone.source);
        get_placed(out, clone.placement);
        out << scale_tool::get_token(clone.placement.scale) << twist_token(clone.placement);
        get_comment(out, clone.source);
    }
    for (const auto& object : file.objects) {
        out << "object";
        get_hex(out, object.id);
        for (const float v : object.pos) {
            get_number(out, v);
        }
        for (const float v : object.rows) {
            get_number(out, v);
        }
        out << scale_tool::get_token(object.scale);
        get_comment(out, object.id);
    }
    for (const auto& copy : file.copies) {
        out << "copy";
        get_hex(out, copy.name);
        get_hex(out, copy.source);
        for (const float v : copy.pos) {
            get_number(out, v);
        }
        for (const float v : copy.rows) {
            get_number(out, v);
        }
        out << scale_tool::get_token(copy.scale);
        get_comment(out, copy.source);
    }
    for (const node_create::Spec& spec : file.added) {
        write_added_node(out, spec);
    }
    if (!file.crown_order_.empty()) {
        out << "order crown";
        for (const std::string& name : file.crown_order_) {
            out << ' ' << name;
        }
        out << '\n';
    }
    for (const uint32_t name : file.hidden) {
        out << "hide";
        get_hex(out, name);
        get_comment(out, name);
    }
    for (const picking::Lock& lock : file.locks) {
        out << (lock.kind == picking::kObjects ? "lock object" : "lock sector");
        get_hex(out, lock.id);
        get_comment(out, lock.id);
    }
    for (const triggers::Placed& trigger : file.triggers) {
        out << "trigger " << triggers::link_record(trigger) << '\n';
    }
    for (const triggers::ProximEdit& proxim : file.proxims) {
        out << "proxim " << triggers::proxim_record(proxim, object_edit::get_name_of(proxim.node)) << '\n';
    }
    for (const SetRecord& node_record : file.nodes) {
        if (!node_record.has_pos_ && !node_record.has_angles_ && node_record.fields.empty()) {
            continue;
        }
        write_set_record(out, node_record);
    }
    out << chains::write_records(file.chains);
    out << entities::write_records(file.entities);
    for (const walls::WallState& w : file.walls) {
        out << "wall";
        get_hex(out, w.checksum);
        get_placed(out, w.placement);
        out << ' ' << (w.off ? 1 : 0);
        get_comment(out, w.checksum);
    }
    // (map3) collision faces' flags, scenery clusters, camera nodes
    out << faces::write_records(file.faces);
    for (const clusters::Membership& cluster : file.clusters) {
        out << clusters::write_record(cluster);
        get_comment(out, cluster.sector);
    }
    for (const cameras::ViewCam& camera : file.cameras) {
        out << cameras::write_record(camera) << '\n';
    }
    for (const goal_edit::Goal& goal : file.goals) {
        out << "goal " << goal_edit::write_record(goal) << '\n'; // (level_editor_goals.h)
    }
    for (const links::LinkRecord& link : file.links) {
        out << links::write_record(link) << '\n'; // (UX7: linked copies)
    }
    for (const std::string& line : file.extra) {
        out << line << '\n';
    }
    // (UX5) note pins; the records that didn't resolve when loaded, kept (level_editor_checks.h)
    for (const notes::Note& note : file.notes) {
        out << notes::write_record(note) << '\n';
    }
    if (!file.carried.empty()) {
        out << "# not found in the level when loaded - kept until the Problems window drops them\n";
        for (const std::string& line : file.carried) {
            out << line << '\n';
        }
    }
    return out.str();
}

namespace {

// sector / rail <checksum> <placement> <hidden> [fields]: a scenery piece or a rail point moved / hidden.
bool read_placed_record(const std::string& kind, std::istringstream& in, FileLoad* file)
{
    uint32_t checksum = 0;
    Placement placement = {};
    int hidden = 0;
    const bool ok =
        read_hex(in, &checksum) && read_placed(in, &placement) && (in >> hidden) && read_placed_fields(in, &placement);
    if (ok && kind == "sector") {
        file->sectors.push_back({.checksum = checksum, .placement = placement, .hidden = hidden != 0});
    } else if (ok) {
        file->rails.push_back({.name = checksum, .placement = placement, .hidden = hidden != 0});
    }
    return ok;
}

// clone <source> <placement> [fields]: a copy of a scenery piece.
bool read_clone_record(std::istringstream& in, FileLoad* file)
{
    CloneRecord clone_record = {};
    const bool ok = read_hex(in, &clone_record.source) && read_placed(in, &clone_record.placement) &&
                    read_placed_fields(in, &clone_record.placement);
    if (ok) {
        file->clones.push_back(clone_record);
    }
    return ok;
}

// object <id> <pos> <rows> [scale]: an object moved.
bool read_object_record(std::istringstream& in, FileLoad* file)
{
    ObjectRecord object_record = {};
    bool ok = read_hex(in, &object_record.id);
    for (float& v : object_record.pos) {
        ok = ok && (in >> v);
    }
    for (float& v : object_record.rows) {
        ok = ok && (in >> v);
    }
    ok = ok && read_scale_fields(in, &object_record.scale);
    if (ok) {
        file->objects.push_back(object_record);
    }
    return ok;
}

// copy <name> <source> <pos> <rows> [scale]: a copy of an object.
bool read_copy_record(std::istringstream& in, FileLoad* file)
{
    CopyRecord copy_record = {};
    bool ok = read_hex(in, &copy_record.name) && read_hex(in, &copy_record.source);
    for (float& v : copy_record.pos) {
        ok = ok && (in >> v);
    }
    for (float& v : copy_record.rows) {
        ok = ok && (in >> v);
    }
    ok = ok && read_scale_fields(in, &copy_record.scale);
    if (ok) {
        file->copies.push_back(copy_record);
    }
    return ok;
}

// set <name> key=value...: a node moved / turned, its fields edited.
bool read_set_record(std::istringstream& in, FileLoad* file)
{
    // (key=value fields: pos and angles; others - a later version's - skipped)
    SetRecord set_record = {};
    bool ok = read_hex(in, &set_record.name);
    std::string field;
    while (ok && in >> field) {
        const size_t equals = field.find('=');
        if (equals == std::string::npos || equals == 0) {
            ok = false;
            break;
        }
        const std::string key = field.substr(0, equals);
        const std::string value = field.substr(equals + 1);
        if (key == "pos") {
            ok = !set_record.has_pos_ && read_triple(value, set_record.pos); // (twice: not a record we wrote)
            set_record.has_pos_ = true;
        } else if (key == "angles") {
            ok = !set_record.has_angles_ && read_triple(value, set_record.angles);
            set_record.has_angles_ = true;
        } else if (fields::is_field_key(key)) {
            // (phase 6: a typed field edit; the same field twice isn't a record we wrote)
            fields::Edit edit;
            ok = fields::read_token({.key = key, .value = value}, &edit);
            for (const fields::Edit& other : set_record.fields) {
                ok = ok && other.key != edit.key;
            }
            if (ok) {
                set_record.fields.push_back(edit);
            }
        }
    }
    if (ok) {
        file->nodes.push_back(set_record);
    }
    return ok;
}

// order crown <names>: the added crowns' order.
bool read_order_record(std::istringstream& in, FileLoad* file)
{
    std::string what;
    std::string name;
    bool ok = static_cast<bool>(in >> what);
    if (ok && what == "crown") { // (an order of something else: a later version's - skipped)
        file->crown_order_.clear();
        while (ok && in >> name) {
            ok = node_create::valid_name(name);
            file->crown_order_.push_back(name);
        }
    }
    return ok;
}

// lock object|sector <id>.
bool read_lock_record(std::istringstream& in, FileLoad* file)
{
    std::string what;
    uint32_t id = 0;
    const bool ok = (in >> what) && read_hex(in, &id);
    if (ok && (what == "object" || what == "sector")) {
        file->locks.push_back({.kind = what == "object" ? picking::kObjects : picking::kScenery, .id = id});
    }
    return ok;
}

// wall <checksum> <placement> <off>.
bool read_wall_record(std::istringstream& in, FileLoad* file)
{
    // (level_editor_walls.h: later key=value fields skipped, as a sector's)
    walls::WallState w = {};
    int off = 0;
    const bool ok = read_hex(in, &w.checksum) && read_placed(in, &w.placement) && (in >> off) && (off == 0 || off == 1);
    w.off = off != 0;
    w.placement.scale = {.x = 1.0F, .y = 1.0F, .z = 1.0F};
    if (ok) {
        file->walls.push_back(w);
    }
    return ok;
}

} // namespace

bool get_take_in(const std::string& text, FileLoad* file, int* line_number)
{
    *file = FileLoad{};
    std::istringstream lines(text);
    std::string line;
    int number = 0;
    bool header = false;
    while (std::getline(lines, line)) {
        ++number;
        *line_number = number;
        if (const size_t hash = line.find('#'); hash != std::string::npos) {
            line.resize(hash);
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::istringstream in(line);
        std::string kind;
        if (!(in >> kind)) {
            continue;
        }
        if (!header) {
            file->version = header_version(line);
            if (file->version == 0) {
                return false;
            }
            header = true;
            continue;
        }
        bool ok = true;
        if (kind == "scene") {
            ok = read_hex(in, &file->scene);
        } else if (kind == "sector" || kind == "rail") {
            ok = read_placed_record(kind, in, file);
        } else if (kind == "clone") {
            ok = read_clone_record(in, file);
        } else if (kind == "object") {
            ok = read_object_record(in, file);
        } else if (kind == "copy") {
            ok = read_copy_record(in, file);
        } else if (kind == "set") {
            ok = read_set_record(in, file);
        } else if (kind == "node") {
            ok = read_node(in, &file->added.emplace_back());
        } else if (kind == "order") {
            ok = read_order_record(in, file);
        } else if (kind == "hide") {
            uint32_t name = 0;
            ok = read_hex(in, &name);
            if (ok) {
                file->hidden.push_back(name);
            }
        } else if (kind == "lock") {
            ok = read_lock_record(in, file);
        } else if (kind == "trigger") {
            std::string rest;
            std::getline(in, rest);
            ok = triggers::read_record(rest.substr(std::min(rest.size(), rest.find_first_not_of(' '))),
                                       &file->triggers.emplace_back());
        } else if (kind == "proxim") {
            std::string rest;
            std::getline(in, rest);
            ok = triggers::read_proxim_record(rest, &file->proxims.emplace_back());
        } else if (kind == "wall") {
            ok = read_wall_record(in, file);
        } else if (chains::is_record(kind)) {
            ok = chains::read_record(kind, in, &file->chains); // (phase 4: point / links / drop / terrain)
        } else if (entities::is_record(kind)) {
            ok = entities::read_record(kind, in, &file->entities); // (speed / emitter)
        } else if (kind == "level") {
            ok = level_props::read_record(in, &file->level); // (phase 6: the level's properties)
        } else if (kind == "faces") {
            ok = faces::read_record(in, &file->faces.emplace_back()); // (map3: level_editor_faces.h)
        } else if (kind == "cluster") {
            ok = clusters::read_record(in, &file->clusters.emplace_back()); // (map3: level_editor_clusters.h)
        } else if (kind == "camera") {
            ok = cameras::read_record(in, &file->cameras.emplace_back()); // (map3: level_editor_cameras.h)
        } else if (kind == "goal") {
            std::string rest;
            std::getline(in, rest);
            ok = goal_edit::read_record(rest, &file->goals.emplace_back()); // (level_editor_goals.h)
        } else if (kind == "note") {
            ok = notes::read_record(in, &file->notes.emplace_back()); // (UX5: level_editor_notes.h)
        } else if (kind == "link") {
            ok = links::read_record(in, &file->links.emplace_back()); // (UX7: level_editor_links.h)
        } else {
            // (anything else: another module's record or a later version's - kept as it is: add_extension)
            const size_t first = line.find_first_not_of(" \t");
            const size_t last = line.find_last_not_of(" \t");
            file->extra.push_back(line.substr(first, last - first + 1));
        }
        if (!ok) {
            return false;
        }
    }
    *line_number = 0;
    return header;
}

} // namespace frontend::level_editor::layout
