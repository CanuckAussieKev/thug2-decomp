// NOT ORIGINAL - the level editor's saved layouts (ours). See level_editor_layout.h.

#include "editor/level_editor_layout.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <span>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include <array>

#include "engine/core/checksums.h"
#include "editor/level_editor.h"
#include "editor/level_editor_career.h"
#include "editor/level_editor_checks.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_scale.h"
#include "engine/gfx/screen.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_manager.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/node_manager.h"
#include "engine/script/param_struct.h"
#include "game/skate/game_mode.h"
#include "platform/crash.h"
#include "platform/run_log.h"
#include "game/skate/skate_module.h"
#include "engine/util/unique_handle.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace frontend::level_editor::layout {

// --- the text (pure) --------------------------------------------------------------------------------------------

namespace {

constexpr auto kHeader = std::to_array("thug2-decomp layout "); // (and the version)

// The header's version (0: not a layout's header).
int header_version(const std::string& line)
{
    if (!line.starts_with(kHeader.data()))
        return 0;
    std::istringstream in(line.substr(sizeof(kHeader) - 1));
    int version = 0;
    std::string rest;
    if (!(in >> version) || version < 1 || (in >> rest))
        return 0;
    return version;
}

// "x,y,z" (three numbers, commas between).
bool read_triple(const std::string& text, std::span<float, 3> out)
{
    const char* at = text.c_str();
    for (int i = 0; i < 3; ++i) {
        char* end = nullptr;
        core::at(out, i) = std::strtof(at, &end);
        if (end == at || !std::isfinite(core::at(out, i)))
            return false;
        at = end;
        if (i < 2) {
            if (*at != ',')
                return false;
            ++at;
        }
    }
    return *at == '\0';
}

void triple(std::ostringstream& out, const char* key, std::span<const float, 3> value)
{
    std::array<char, 96> text{};
    std::snprintf(text.data(), sizeof(text), " %s=%.9g,%.9g,%.9g", key, core::at(value, 0), core::at(value, 1), core::at(value, 2));
    out << text.data();
}

void get_number(std::ostringstream& out, float value)
{
    std::array<char, 32> text{};
    std::snprintf(text.data(), sizeof(text), " %.9g", value);
    out << text.data();
}

void get_hex(std::ostringstream& out, uint32_t value)
{
    std::array<char, 16> text{};
    std::snprintf(text.data(), sizeof(text), " %08X", value);
    out << text.data();
}

void get_placed(std::ostringstream& out, const Placement& p)
{
    out << ' ' << p.quarters;
    get_number(out, p.offset.x);
    get_number(out, p.offset.y);
    get_number(out, p.offset.z);
}

void get_comment(std::ostringstream& out, uint32_t checksum)
{
    if (const char* name = object_edit::get_name_of(checksum))
        out << "  # " << name;
    out << '\n';
}

bool read_hex(std::istringstream& in, uint32_t* value)
{
    std::string word;
    if (!(in >> word) || word.empty() || word.size() > 8)
        return false;
    char* end = nullptr;
    *value = static_cast<uint32_t>(std::strtoul(word.c_str(), &end, 16));
    return end && *end == '\0';
}

bool read_placed(std::istringstream& in, Placement* p)
{
    return static_cast<bool>(in >> p->quarters >> p->offset.x >> p->offset.y >> p->offset.z) && p->quarters >= 0 &&
           p->quarters <= 3;
}

// The key=value fields after a sector / rail / clone / object / copy record's numbers: scale= (level_editor_scale.h);
// others (a later version's) skipped. False: a bad scale.
bool read_scale_fields(std::istringstream& in, V3* scale)
{
    std::string field;
    while (in >> field) {
        if (field.starts_with("scale=")) {
            if (!scale_tool::read_token(field, scale))
                return false;
        }
    }
    return true;
}
bool read_placed_fields(std::istringstream& in, Placement* p) { return read_scale_fields(in, &p->scale); }

// A `node` record's fields (after "node"): name, class, then key=value (unknown keys skipped; a repeated one, a bad
// value or a missing place refuse it).
bool read_node(std::istringstream& in, node_create::Spec* spec)
{
    if (!(in >> spec->name >> spec->cls) || !node_create::valid_name(spec->name) || !node_create::valid_name(spec->cls))
        return false;
    std::vector<std::string> seen;
    std::string field;
    while (in >> field) {
        const size_t equals = field.find('=');
        if (equals == std::string::npos || equals == 0)
            return false;
        const std::string key = field.substr(0, equals);
        const std::string value = field.substr(equals + 1);
        if (std::ranges::find(seen, key) != seen.end())
            return false;
        seen.push_back(key);
        std::array<float, 3> v{};
        char* end = nullptr;
        if (key == "type") {
            if (!node_create::valid_name(value))
                return false;
            spec->type = value;
        } else if (key == "pos" || key == "angles") {
            if (!read_triple(value, v))
                return false;
            (key == "pos" ? spec->pos : spec->angles) = {core::at(v, 0), core::at(v, 1), core::at(v, 2)};
        } else if (key == "types") {
            std::istringstream list(value);
            std::string type;
            while (std::getline(list, type, ',')) {
                if (!node_create::valid_name(type))
                    return false;
                spec->types.push_back(type);
            }
        } else if (key == "color") {
            if (!read_triple(value, v))
                return false;
            for (int i = 0; i < 3; ++i) {
                if (core::at(v, i) < 0.0f || core::at(v, i) > 255.0f || core::at(v, i) != static_cast<float>(static_cast<int>(core::at(v, i))))
                    return false;
                core::at(spec->colour, i) = static_cast<int>(core::at(v, i));
            }
        } else if (key == "brightness" || key == "inner" || key == "outer" || key == "radius") {
            const float number = std::strtof(value.c_str(), &end);
            if (value.empty() || *end != '\0' || !std::isfinite(number))
                return false;
            (key == "brightness" ? spec->brightness
             : key == "inner"    ? spec->inner
             : key == "outer"    ? spec->outer
                                 : spec->radius) = number;
        }
    }
    return std::ranges::find(seen, "pos") != seen.end();
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
    for (const auto& s : file.sectors) {
        out << "sector";
        get_hex(out, s.checksum);
        get_placed(out, s.placement);
        out << ' ' << (s.hidden ? 1 : 0) << scale_tool::get_token(s.placement.scale);
        get_comment(out, s.checksum);
    }
    for (const auto& r : file.rails) {
        out << "rail";
        get_hex(out, r.name);
        get_placed(out, r.placement);
        out << ' ' << (r.hidden ? 1 : 0) << scale_tool::get_token(r.placement.scale);
        get_comment(out, r.name);
    }
    for (const auto& c : file.clones) {
        out << "clone";
        get_hex(out, c.source);
        get_placed(out, c.placement);
        out << scale_tool::get_token(c.placement.scale);
        get_comment(out, c.source);
    }
    for (const auto& o : file.objects) {
        out << "object";
        get_hex(out, o.id);
        for (const float v : o.pos)
            get_number(out, v);
        for (const float v : o.rows)
            get_number(out, v);
        out << scale_tool::get_token(o.scale);
        get_comment(out, o.id);
    }
    for (const auto& c : file.copies) {
        out << "copy";
        get_hex(out, c.name);
        get_hex(out, c.source);
        for (const float v : c.pos)
            get_number(out, v);
        for (const float v : c.rows)
            get_number(out, v);
        out << scale_tool::get_token(c.scale);
        get_comment(out, c.source);
    }
    for (const node_create::Spec& a : file.added) {
        out << "node " << a.name << ' ' << a.cls;
        if (!a.type.empty())
            out << " type=" << a.type;
        const std::array<float, 3> pos = {{a.pos.x, a.pos.y, a.pos.z}};
        const std::array<float, 3> angles = {{a.angles.x, a.angles.y, a.angles.z}};
        triple(out, "pos", pos);
        triple(out, "angles", angles);
        if (!a.types.empty()) {
            out << " types=";
            for (size_t i = 0; i < a.types.size(); ++i)
                out << (i ? "," : "") << core::at(a.types, i);
        }
        node_create::Kind kind{};
        if (node_create::kind_of(a, &kind) && kind == node_create::Kind::kLight) {
            out << " color=" << core::at(a.colour, 0) << ',' << core::at(a.colour, 1) << ',' << core::at(a.colour, 2);
            std::array<char, 96> text{};
            std::snprintf(text.data(), sizeof(text), " brightness=%.9g inner=%.9g outer=%.9g", a.brightness, a.inner, a.outer);
            out << text.data();
        }
        if (a.radius != 0.0f) {
            std::array<char, 32> text{};
            std::snprintf(text.data(), sizeof(text), " radius=%.9g", a.radius);
            out << text.data();
        }
        out << '\n';
    }
    if (!file.crown_order_.empty()) {
        out << "order crown";
        for (const std::string& name : file.crown_order_)
            out << ' ' << name;
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
    for (const triggers::Placed& t : file.triggers)
        out << "trigger " << triggers::link_record(t) << '\n';
    for (const triggers::ProximEdit& p : file.proxims)
        out << "proxim " << triggers::proxim_record(p, object_edit::get_name_of(p.node)) << '\n';
    for (const SetRecord& s : file.nodes) {
        if (!s.has_pos_ && !s.has_angles_ && s.fields.empty())
            continue;
        out << "set";
        get_hex(out, s.name);
        if (s.has_pos_)
            triple(out, "pos", s.pos);
        if (s.has_angles_)
            triple(out, "angles", s.angles);
        for (const fields::Edit& edit : s.fields)
            out << ' ' << fields::get_token(edit, fields::get_name_of);
        get_comment(out, s.name);
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
    for (const clusters::Membership& m : file.clusters) {
        out << clusters::write_record(m);
        get_comment(out, m.sector);
    }
    for (const cameras::ViewCam& c : file.cameras)
        out << cameras::write_record(c) << '\n';
    for (const goal_edit::Goal& g : file.goals)
        out << "goal " << goal_edit::write_record(g) << '\n'; // (level_editor_goals.h)
    for (const links::LinkRecord& r : file.links)
        out << links::write_record(r) << '\n'; // (UX7: linked copies)
    for (const std::string& line : file.extra)
        out << line << '\n';
    // (UX5) note pins; the records that didn't resolve when loaded, kept (level_editor_checks.h)
    for (const notes::Note& n : file.notes)
        out << notes::write_record(n) << '\n';
    if (!file.carried.empty()) {
        out << "# not found in the level when loaded - kept until the Problems window drops them\n";
        for (const std::string& line : file.carried)
            out << line << '\n';
    }
    return out.str();
}

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
        if (const size_t hash = line.find('#'); hash != std::string::npos)
            line.resize(hash);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::istringstream in(line);
        std::string kind;
        if (!(in >> kind))
            continue;
        if (!header) {
            file->version = header_version(line);
            if (!file->version)
                return false;
            header = true;
            continue;
        }
        bool ok = true;
        if (kind == "scene") {
            ok = read_hex(in, &file->scene);
        } else if (kind == "sector" || kind == "rail") {
            uint32_t checksum = 0;
            Placement p = {};
            int hidden = 0;
            ok = read_hex(in, &checksum) && read_placed(in, &p) && (in >> hidden) && read_placed_fields(in, &p);
            if (ok && kind == "sector")
                file->sectors.push_back({checksum, p, hidden != 0});
            else if (ok)
                file->rails.push_back({checksum, p, hidden != 0});
        } else if (kind == "clone") {
            CloneRecord c = {};
            ok = read_hex(in, &c.source) && read_placed(in, &c.placement) && read_placed_fields(in, &c.placement);
            if (ok)
                file->clones.push_back(c);
        } else if (kind == "object") {
            ObjectRecord o = {};
            ok = read_hex(in, &o.id);
            for (float& v : o.pos)
                ok = ok && (in >> v);
            for (float& v : o.rows)
                ok = ok && (in >> v);
            ok = ok && read_scale_fields(in, &o.scale);
            if (ok)
                file->objects.push_back(o);
        } else if (kind == "copy") {
            CopyRecord c = {};
            ok = read_hex(in, &c.name) && read_hex(in, &c.source);
            for (float& v : c.pos)
                ok = ok && (in >> v);
            for (float& v : c.rows)
                ok = ok && (in >> v);
            ok = ok && read_scale_fields(in, &c.scale);
            if (ok)
                file->copies.push_back(c);
        } else if (kind == "set") {
            // (key=value fields: pos and angles; others - a later version's - skipped)
            SetRecord s = {};
            ok = read_hex(in, &s.name);
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
                    ok = !s.has_pos_ && read_triple(value, s.pos); // (twice: not a record we wrote)
                    s.has_pos_ = true;
                } else if (key == "angles") {
                    ok = !s.has_angles_ && read_triple(value, s.angles);
                    s.has_angles_ = true;
                } else if (fields::is_field_key(key)) {
                    // (phase 6: a typed field edit; the same field twice isn't a record we wrote)
                    fields::Edit edit;
                    ok = fields::read_token(key, value, &edit);
                    for (const fields::Edit& other : s.fields)
                        ok = ok && other.key != edit.key;
                    if (ok)
                        s.fields.push_back(edit);
                }
            }
            if (ok)
                file->nodes.push_back(s);
        } else if (kind == "node") {
            ok = read_node(in, &file->added.emplace_back());
        } else if (kind == "order") {
            std::string what, name;
            ok = static_cast<bool>(in >> what);
            if (ok && what == "crown") { // (an order of something else: a later version's - skipped)
                file->crown_order_.clear();
                while (ok && in >> name) {
                    ok = node_create::valid_name(name);
                    file->crown_order_.push_back(name);
                }
            }
        } else if (kind == "hide") {
            uint32_t name = 0;
            ok = read_hex(in, &name);
            if (ok)
                file->hidden.push_back(name);
        } else if (kind == "lock") {
            std::string what;
            uint32_t id = 0;
            ok = (in >> what) && read_hex(in, &id);
            if (ok && (what == "object" || what == "sector"))
                file->locks.push_back({what == "object" ? picking::kObjects : picking::kScenery, id});
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
            // (level_editor_walls.h: later key=value fields skipped, as a sector's)
            walls::WallState w = {};
            int off = 0;
            ok = read_hex(in, &w.checksum) && read_placed(in, &w.placement) && (in >> off) && (off == 0 || off == 1);
            w.off = off != 0;
            w.placement.scale = {1.0f, 1.0f, 1.0f};
            if (ok)
                file->walls.push_back(w);
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
            const size_t a = line.find_first_not_of(" \t");
            const size_t b = line.find_last_not_of(" \t");
            file->extra.push_back(line.substr(a, b - a + 1));
        }
        if (!ok)
            return false;
    }
    *line_number = 0;
    return header;
}

// --- the autosave's names and times (pure) ---------------------------------------------------------------------

std::string saved_file_name(uint32_t scene)
{
    std::array<char, 32> name{};
    std::snprintf(name.data(), sizeof(name), "%08X.txt", scene);
    return name.data();
}

std::string autosave_file_name(uint32_t scene)
{
    std::array<char, 32> name{};
    std::snprintf(name.data(), sizeof(name), "%08X.autosave.txt", scene);
    return name.data();
}

bool autosave_newer(bool have_autosave, uint64_t autosave_time, bool have_saved, uint64_t saved_time)
{
    return have_autosave && (!have_saved || autosave_time > saved_time);
}

// --- the edited copy (pure) --------------------------------------------------------------------------------------

bool apply_edited_copy(uint32_t request, uint32_t level_load, bool network)
{
    return !network && request != 0 && request == level_load;
}

uint32_t request_after_load(uint32_t request, uint32_t level_load, bool network)
{
    return network || request != level_load ? 0 : request;
}

uint32_t request_from_pick(uint32_t load, bool edited, bool network) { return edited && !network ? load : 0; }

bool network_session(bool online, bool net_game, bool net_scene) { return online || net_game || net_scene; }

const char* mode_refusal(const ModeFlags& mode)
{
    if (!mode.known)
        return "no game mode";
    if (mode.net)
        return "a network game mode";
    if (mode.career)
        return "story mode";
    if (mode.classic)
        return "classic mode";
    if (mode.park_editor_)
        return "the park editor";
    if (mode.create_goals_)
        return "create-a-goal";
    if (mode.freeskate || mode.single_session_)
        return nullptr;
    return "a competitive game mode";
}

std::string edited_text(const char* text) { return std::string(text && *text ? text : "LEVEL") + " (EDITED)"; }

std::string edited_title(const char* level_text, const std::string& name)
{
    return name.empty() ? edited_text(level_text) : name;
}

std::string sign_label(const std::string& name)
{
    if (name.empty())
        return "EDITED";
    if (name.size() <= kSignLabelMax)
        return name;
    return name.substr(0, kSignLabelMax - 1) + ".";
}

bool saved_layout_scene(const std::string& file_name, uint32_t* scene)
{
    // (exactly "XXXXXXXX.txt": "XXXXXXXX.autosave.txt" and "XXXXXXXX.txt.tmp" aren't)
    if (file_name.size() != 12 || _stricmp(file_name.c_str() + 8, ".txt") != 0)
        return false;
    uint32_t value = 0;
    for (int i = 0; i < 8; ++i) {
        const char c = core::at(file_name, i);
        const int digit = c >= '0' && c <= '9'   ? c - '0'
                          : c >= 'A' && c <= 'F' ? c - 'A' + 10
                          : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                                 : -1;
        if (digit < 0)
            return false;
        value = value << 4 | static_cast<uint32_t>(digit);
    }
    *scene = value;
    return true;
}

uint32_t scene_of_level_text(const char* level) { return level && *level ? checksums::hash_from_text(level) : 0; }

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kSkater = 0x5B8AB877; // Skater (the local skater's alias)
constexpr int kSettleFrames = 60;        // (a level's objects and rails are made after its scenes: wait a second)

bool g_pending;
int g_wait;
int g_level_loads;

// Other modules' records (add_extension).
std::vector<Extension>& get_extensions()
{
    static std::vector<Extension> extensions;
    return extensions;
}
void reset_extensions()
{
    for (const Extension& e : get_extensions()) {
        if (e.reset)
            e.reset();
    }
}
void apply_extensions(const std::vector<std::string>& extra)
{
    for (const Extension& e : get_extensions()) {
        if (!e.apply)
            continue;
        std::vector<std::string> mine;
        for (const std::string& line : extra) {
            const std::string word = line.substr(0, line.find(' '));
            if (std::ranges::find(e.keywords, word) != e.keywords.end())
                mine.push_back(line);
        }
        e.apply(mine);
    }
}

// The autosave: the text made by the last Snapshot and where it goes (plain memory the crash hook can write without
// allocating: `ready` is false while they change), whether it has been written since, whether this run wrote (or
// restored) this level's autosave, and whether an older one is being offered (nothing written over it meanwhile).
std::string g_snapshot;
std::array<char, MAX_PATH> g_snapshot_path;
std::array<char, MAX_PATH> g_snapshot_folder;
volatile bool g_snapshot_ready;
volatile bool g_snapshot_written = true;
bool g_autosave_ours;
bool g_offer_pending;

// The edited copy: the level picked as edited (its load script; 0: none - LevelEditor_PickLevel), whether the level
// loaded now is its edited copy, whether the last scene was loaded for a network game, and a switch asked for in place
// (the level picked is the one loaded: +1 to the edited copy, -1 back to the level as shipped) and its wait.
uint32_t g_request;
bool g_edited_copy;
bool g_net_scene;
int g_switch;
int g_switch_wait;

// The network game (online::Hub 0x007CCE14, 0x0044E4F0: a network game is on - host or client).
bool in_net_game()
{
    void* net = online::g_online_hub;
    return net && static_cast<online::Hub*>(net)->in_net_game();
}

// The level loaded: its load script (the skate module 0x007CE478, +0x378 - as automation.h and the node layer read it).
uint32_t current_load()
{
    const modules::Skate* skate = modules::g_skate_module;
    return skate ? skate->level_ : 0;
}

std::string get_folder() { return game_folder_path("thug2-decomp-layouts"); }

std::string path_for(uint32_t scene) { return get_folder() + "\\" + saved_file_name(scene); }
std::string autosave_path_for(uint32_t scene) { return get_folder() + "\\" + autosave_file_name(scene); }

// A file's last-write time (false: no such file).
bool write_time(const std::string& path, uint64_t* time)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data) ||
        (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return false;
    *time = (static_cast<uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
    return true;
}

// `size` bytes written to `path` through `path`.tmp (a crash while writing leaves the last autosave whole), the folder
// made first. No allocation: the crash hook uses it.
bool write_whole(const char* folder, const char* path, const char* data, size_t size)
{
    std::array<char, MAX_PATH + 8> temporary{};
    if (std::snprintf(temporary.data(), sizeof(temporary), "%s.tmp", path) <= 0)
        return false;
    CreateDirectoryA(folder, nullptr);
    util::UniqueHandle file(
        CreateFileA(temporary.data(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file)
        return false;
    DWORD written = 0;
    bool ok = WriteFile(file.get(), data, static_cast<DWORD>(size), &written, nullptr) && written == size;
    file.reset(); // (closed before it's moved)
    ok = ok && MoveFileExA(temporary.data(), path, MOVEFILE_REPLACE_EXISTING);
    if (!ok)
        DeleteFileA(temporary.data());
    return ok;
}

void rows_of(const maths::Mat4& m, std::span<float, 9> rows)
{
    for (int r = 0; r < 3; ++r) {
        core::at(rows, r * 3 + 0) = core::at(m.row, r).x;
        core::at(rows, r * 3 + 1) = core::at(m.row, r).y;
        core::at(rows, r * 3 + 2) = core::at(m.row, r).z;
    }
}

object_edit::State state_of(uint32_t id, std::span<const float, 3> pos, std::span<const float, 9> rows, V3 scale)
{
    object_edit::State state = {};
    state.id = id;
    state.scale = scale;
    state.pos = {core::at(pos, 0), core::at(pos, 1), core::at(pos, 2), 1.0f};
    for (int r = 0; r < 3; ++r) {
        core::at(state.matrix.row, r) = {core::at(rows, r * 3 + 0), core::at(rows, r * 3 + 1), core::at(rows, r * 3 + 2), 0.0f};
        core::at(state.draw_matrix_.row, r) = core::at(state.matrix.row, r);
    }
    core::at(state.matrix.row, 3) = core::at(state.draw_matrix_.row, 3) = {0.0f, 0.0f, 0.0f, 1.0f};
    return state;
}

objects::GameEntity* live_object(uint32_t id)
{
    auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
    return object && !(object->object_flags_ & objects::kObjectFlagDead) ? object : nullptr;
}

// The level's changes as they are now (false: not in a level).
bool get_capture(FileLoad* file)
{
    // (an object the editor's preview is moving along its path: read where it stands - level_editor_entities.h)
    struct PreviewHold {
        PreviewHold(const PreviewHold&) = delete;
        PreviewHold& operator=(const PreviewHold&) = delete;
        PreviewHold() { entities::preview_suspend(); }
        ~PreviewHold() { entities::preview_resume(); }
    };
    const PreviewHold hold;
    render::WorldScene* scene = scenery::main_scene();
    if (render::current_level() == 0 || !scene)
        return false;
    *file = FileLoad{};
    file->scene = scenery::world_scene_key(scene);
    file->sectors = scenery::changed_sectors();
    file->rails = scenery::changed_rails();
    // (a rail point the editor added: its `point` record has where it is now - not a `rail` offset)
    std::erase_if(file->rails, [](const scenery::RailState& r) { return node_create::is_added(r.name); });
    std::vector<uint32_t> clone_order; // (UX7: the linked copies' clones by their records' order)
    for (const scenery::Replicate& clone : scenery::get_clones()) {
        Placement now;
        if (scenery::placement_now(clone.checksum, &now) && !scenery::get_hidden(clone.checksum)) {
            file->clones.push_back({clone.source, compose(now, clone.source_at_copy_)});
            clone_order.push_back(clone.checksum);
        }
    }
    file->links = links::get_capture(clone_order);
    for (const uint32_t id : object_edit::get_touched()) {
        objects::GameEntity* object = live_object(id);
        if (!object)
            continue;
        ObjectRecord record = {id, {object->at_point_.x, object->at_point_.y, object->at_point_.z}, {}};
        rows_of(object->basis_frame_, record.rows);
        record.scale = scale_tool::get_object_scale(object);
        file->objects.push_back(record);
    }
    for (const object_edit::DupFrom& copy : object_edit::get_copies()) {
        objects::GameEntity* object = live_object(copy.name);
        if (!object)
            continue;
        CopyRecord record = {
            copy.name, copy.source, {object->at_point_.x, object->at_point_.y, object->at_point_.z}, {}};
        rows_of(object->basis_frame_, record.rows);
        record.scale = scale_tool::get_object_scale(object);
        file->copies.push_back(record);
    }
    // Added nodes (as `node` records, where they are now; the crowns' order is theirs), hidden ones.
    // (rail / path points: the chains' `point` records; the level's points deleted: `drop`, not `hide`)
    file->chains = chains::at_hand();
    file->entities = entities::at_hand(); // (speeds, emitters' sounds)
    file->added = node_create::at_hand();
    // (a trigger volume's node: its own `trigger` record - level_editor_triggers.h)
    std::erase_if(file->added, [](const node_create::Spec& spec) { return spec.owned; });
    file->triggers = triggers::capture_added();
    file->proxims = triggers::capture_proxims();
    std::erase_if(file->added, [](const node_create::Spec& s) { return chains::is_chain_class(s.cls); });
    const std::vector<std::string> crowns = node_create::crown_order(file->added);
    if (crowns.size() > 1)
        file->crown_order_ = crowns;
    for (const uint32_t name : node_create::hidden_names()) {
        if (std::ranges::find(file->chains.dropped, name) == file->chains.dropped.end())
            file->hidden.push_back(name);
    }
    file->locks = picking::get_locks();
    file->walls = walls::get_changed();
    for (const node_edit::LinkRecord& node : node_edit::get_changed()) {
        if (node_create::is_added(node.name))
            continue; // (its node record has its place)
        const node_edit::Values& v = node.values;
        file->nodes.push_back(
            {node.name, true, true, {v.pos.x, v.pos.y, v.pos.z}, {v.angles.x, v.angles.y, v.angles.z}});
    }
    // (phase 6) the Inspector's field edits: on the node's set record (one made for a node not moved)
    for (const fields::NodeEdits& node : fields::get_changed()) {
        auto it = std::ranges::find_if(file->nodes, [&](const SetRecord& s) { return s.name == node.name; });
        if (it == file->nodes.end()) {
            SetRecord record = {};
            record.name = node.name;
            file->nodes.push_back(record);
            it = file->nodes.end() - 1;
        }
        it->fields = node.edits;
    }
    file->level = level_props::at_hand();
    // (map3) collision faces' flags, scenery clusters, camera nodes (their nodes are owned: not `node` records)
    file->faces = faces::get_changed();
    file->clusters = clusters::get_changed();
    file->cameras = cameras::get_capture();
    file->goals = goal_edit::get_capture(); // (their points are owned nodes: not `node` records)
    for (const Extension& e : get_extensions()) {
        if (!e.capture)
            continue;
        for (const std::string& line : e.capture())
            file->extra.push_back(line);
    }
    file->notes = notes::get_capture(); // (UX5)
    // (UX5) the loaded layout's records that didn't resolve, kept unless captured again (level_editor_checks.h)
    file->carried = checks::carried_records(emit_out(*file));
    return true;
}

// A layout file of this level read (false: none - `message` - or not one; logged).
bool read_for(const std::string& path, uint32_t scene, FileLoad* file, std::string* message, const char* none)
{
    const std::ifstream in(path, std::ios::binary);
    if (!in) {
        *message = none;
        return false;
    }
    std::stringstream text;
    text << in.rdbuf();
    int line = 0;
    if (!get_take_in(text.str(), file, &line) || file->scene != scene) {
        *message = "Couldn't read " + path.substr(path.find_last_of('\\') + 1);
        get_log("level editor: %s %s (line %d)", message->c_str(), path.c_str(), line);
        return false;
    }
    return true;
}

// A layout put in place: copies of sectors made (each of its source as loaded - the sources aren't moved yet - then
// put where they were), sectors and rails placed, objects placed, copies of objects placed (made unless one of that
// name is already there).
std::string get_apply(const FileLoad& file)
{
    std::vector<scenery::SectorState> clones;
    std::vector<uint32_t> clone_made;
    for (const CloneRecord& record : file.clones) {
        const uint32_t made = scenery::get_duplicate(record.source);
        clone_made.push_back(made); // (UX7: by the records' order, 0 for one not made - the linked copies)
        if (made)
            clones.push_back({made, record.placement, false});
    }
    scenery::apply_states(clones, {});
    scenery::apply_states(file.sectors, file.rails);
    int objects = 0, copies = 0;
    for (const ObjectRecord& record : file.objects) {
        if (object_edit::get_restore(state_of(record.id, record.pos, record.rows, record.scale))) {
            object_edit::touch(record.id);
            ++objects;
        }
    }
    for (const CopyRecord& record : file.copies) {
        const bool there = object_edit::is_copy(record.name) && live_object(record.name);
        if ((there ||
             object_edit::make_copy(record.source, {core::at(record.pos, 0), core::at(record.pos, 1), core::at(record.pos, 2)}, record.name)) &&
            object_edit::get_restore(state_of(record.name, record.pos, record.rows, record.scale)))
            ++copies;
    }
    // Nodes added (in the overlay, in order: one re-registration), the added crowns' order, the level's nodes hidden.
    std::string message;
    const int added = file.added.empty() ? 0 : node_create::create_all(file.added, &message);
    if (!file.crown_order_.empty())
        node_create::apply_crown_order(file.crown_order_);
    int hidden = 0;
    for (const uint32_t name : file.hidden) {
        if (node_create::conceal(name, true))
            ++hidden;
    }
    // Level nodes moved / turned (a node the level no longer has is skipped).
    int nodes = 0;
    for (const SetRecord& record : file.nodes) {
        node_edit::Values values;
        if (!node_edit::get_take_in(record.name, &values))
            continue;
        if (record.has_pos_)
            values.pos = {core::at(record.pos, 0), core::at(record.pos, 1), core::at(record.pos, 2)};
        if (record.has_angles_)
            values.angles = {core::at(record.angles, 0), core::at(record.angles, 1), core::at(record.angles, 2)};
        if ((record.has_pos_ || record.has_angles_) && node_edit::assign_value(record.name, values))
            ++nodes;
    }
    // (phase 6) the Inspector's field edits (written into the nodes already if the edited copy's level array got them
    // as it loaded - on_node_array_added; the same values again here), the level's properties.
    std::vector<fields::NodeEdits> edits;
    for (const SetRecord& record : file.nodes) {
        if (!record.fields.empty())
            edits.push_back({record.name, record.fields});
    }
    const int fields = edits.empty() ? 0 : fields::get_apply(edits);
    for (const fields::NodeEdits& node : edits) {
        static const uint32_t kModel = checksums::hash_from_text("model");
        for (const fields::Edit& edit : node.edits) {
            if (edit.key == kModel)
                entities::model_edited(node.name); // (a copy's own model: made again with it)
        }
    }
    level_props::assign_value(file.level);
    picking::set_locks(file.locks);
    // Chains (phase 4): added rail / path points, relinked and dropped level points, terrains - the rail manager built
    // again with them; peds / cars whose path changed made again.
    if (!chains::vacant(file.chains))
        chains::change_phase(file.chains);
    // Collision (level_editor_walls.h): invisible walls moved / switched off, drawn pieces' collision switched off - after
    // the sectors (a piece shown by them gets its switch back).
    walls::apply_states(file.walls);
    entities::change_phase(file.entities); // (after the nodes and copies: the emitters and objects they name)
    walls::tick();
    const int triggers = triggers::get_apply(file.triggers, &message);
    if (!message.empty())
        get_log("level editor: %s", message.c_str());
    if (!file.proxims.empty())
        get_log("level editor: %d of the layout's %u proximity node edits in place",
                triggers::apply_proxims(file.proxims), static_cast<unsigned>(file.proxims.size()));
    // (map3) collision faces' flags - after the sectors (a moved piece's shared collision took its faces along: theirs
    // are set there), scenery clusters (after the level's own registration), camera nodes.
    const int faces = faces::get_apply(file.faces);
    const int clusters = clusters::get_apply(file.clusters);
    const int cameras = cameras::get_apply(file.cameras);
    goal_edit::get_apply(file.goals); // (after the nodes: their points' nodes made; in play unless the editor runs)
    if (const int links =
            links::get_apply(file.links, clone_made)) // (UX7: the linked copies - after their copies were made)
        get_log("level editor: the layout's %d linked-copy groups in place", links);
    apply_extensions(file.extra); // (other modules' records: level_editor_organize.h)
    if (faces || clusters || cameras)
        get_log("level editor: the layout's %d collision faces, %d trick-object clusters, %d cameras put in place",
                faces, clusters, cameras);
    if (const int notes = notes::get_apply(file.notes)) // (UX5: note pins)
        get_log("level editor: the layout's %d notes put in place", notes);
    checks::note_layout_applied(file); // (UX5: records that don't resolve - logged, listed, kept)
    std::array<char, 360> summary{};
    std::snprintf(summary.data(), sizeof(summary),
                  "%u pieces, %u copies, %u rail points, %d objects, %d object copies, %d nodes, %d added, %d hidden, "
                  "%u chain points, %u relinked, %u dropped, %u collision switches / walls, %d triggers, %d fields%s",
                  static_cast<unsigned>(file.sectors.size()), static_cast<unsigned>(clones.size()),
                  static_cast<unsigned>(file.rails.size()), objects, copies, nodes, added, hidden,
                  static_cast<unsigned>(file.chains.points.size()), static_cast<unsigned>(file.chains.links.size()),
                  static_cast<unsigned>(file.chains.dropped.size()), static_cast<unsigned>(file.walls.size()), triggers,
                  fields, level_props::vacant(file.level) ? "" : ", level properties");
    return summary.data();
}

// Everything the editor has changed in this level put back as the level has it, before another layout goes in place
// of it: moved / hidden sectors and rails back, copies of sectors hidden (a layout leaves hidden copies out), moved
// objects back where they first were, copies of objects removed - but those named in `keep` (put in place again by
// Apply).
void reset_to_level(const FileLoad& keep)
{
    // (the chains first: added points removed, links and terrains as the level has them, dropped points shown, the
    // rail manager built again - then the moved rail points below are put back in it)
    chains::change_phase(chains::State{});
    std::vector<scenery::SectorState> sectors = scenery::changed_sectors();
    for (scenery::SectorState& state : sectors) {
        state.placement = {0, {0.0f, 0.0f, 0.0f}};
        state.hidden = false;
    }
    for (const scenery::Replicate& clone : scenery::get_clones()) {
        Placement now = {0, {0.0f, 0.0f, 0.0f}};
        scenery::placement_now(clone.checksum, &now);
        if (!scenery::get_hidden(clone.checksum))
            sectors.push_back({clone.checksum, now, true});
    }
    std::vector<scenery::RailState> rails = scenery::changed_rails();
    for (scenery::RailState& state : rails) {
        state.placement = {0, {0.0f, 0.0f, 0.0f}};
        state.hidden = false;
    }
    scenery::apply_states(sectors, rails);
    walls::reset_all(); // (walls back where the level has them, every collision switch as the level had it)
    for (const uint32_t id : object_edit::get_touched()) {
        if (const object_edit::State* origin = object_edit::get_origin(id))
            object_edit::get_restore(*origin);
    }
    // (a copy: removing a copy changes the list)
    const auto copies = std::vector<object_edit::DupFrom>(object_edit::get_copies());
    for (const object_edit::DupFrom& copy : copies) {
        bool kept = false;
        for (const CopyRecord& record : keep.copies)
            kept = kept || record.name == copy.name;
        if (!kept)
            object_edit::remove_copy(copy.name);
    }
    node_edit::reset_all();
    triggers::reset_all();    // (the editor's trigger volumes and their nodes removed, the level's turned on again)
    node_create::reset_all(); // (added nodes removed, hidden ones shown: Apply makes the layout's again)
    fields::reset_all();
    level_props::assign_value(level_props::Props{});
    entities::change_phase(entities::State{});
    // (map3) every face's flags and every piece's cluster back as the level has them; the cameras went with the added
    // nodes (node_create::reset_all) - Apply makes the layout's again
    faces::reset_all();
    clusters::reset_all();
    cameras::reset_all();
    goal_edit::reset_all(); // (out of play, their points' nodes removed - Apply makes the layout's again)
    links::reset_all();     // (UX7)
    reset_extensions();
    notes::reset_all();              // (UX5)
    checks::forget_layout_records(); // (UX5: Apply notes the next layout's again)
}

// The crash hook's write (no allocation; run_guarded catches a fault in it).
void write_snapshot()
{
    if (write_whole(g_snapshot_folder.data(), g_snapshot_path.data(), g_snapshot.data(), g_snapshot.size()))
        g_snapshot_written = true;
}

} // namespace

void add_extension(const Extension& extension) { get_extensions().push_back(extension); }

bool store_away(std::string* message)
{
    FileLoad file;
    if (!get_capture(&file)) {
        *message = "No level loaded";
        return false;
    }
    CreateDirectoryA(get_folder().c_str(), nullptr);
    const std::string path = path_for(file.scene);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << emit_out(file);
    out.close();
    if (!out) {
        *message = "Couldn't write " + path;
        get_log("level editor: %s", message->c_str());
        return false;
    }
    std::array<char, 256> text{};
    std::snprintf(
        text.data(), sizeof(text),
        "Saved: %u pieces, %u copies, %u rail points, %u objects, %u object copies, %u nodes, %u added, %u hidden, "
        "%u triggers",
        static_cast<unsigned>(file.sectors.size()), static_cast<unsigned>(file.clones.size()),
        static_cast<unsigned>(file.rails.size()), static_cast<unsigned>(file.objects.size()),
        static_cast<unsigned>(file.copies.size()), static_cast<unsigned>(file.nodes.size()),
        static_cast<unsigned>(file.added.size()), static_cast<unsigned>(file.hidden.size()),
        static_cast<unsigned>(file.triggers.size()));
    *message = text.data();
    get_log("level editor: %s - %s", text.data(), path.c_str());
    // (saved: the autosave's changes are in the layout now, or the user chose these over them)
    const std::string autosave = autosave_path_for(file.scene);
    if (DeleteFileA(autosave.c_str()))
        get_log("level editor: autosave deleted - %s", autosave.c_str());
    g_snapshot_ready = false;
    g_snapshot_written = true;
    g_offer_pending = false;
    return true;
}

bool read_in(std::string* message)
{
    render::WorldScene* scene = scenery::main_scene();
    if (render::current_level() == 0 || !scene) {
        *message = "Not in a level";
        return false;
    }
    std::string why;
    if (get_refused(&why)) {
        *message = "Layout not applied: " + why;
        get_log("level editor: %s", message->c_str());
        return false;
    }
    const std::string path = path_for(scenery::world_scene_key(scene));
    FileLoad file;
    if (!read_for(path, scenery::world_scene_key(scene), &file, message, "No saved layout for this level"))
        return false;
    *message = "Layout loaded: " + get_apply(file);
    get_log("level editor: %s - %s", message->c_str(), path.c_str());
    return true;
}

bool get_snapshot()
{
    FileLoad file;
    if (!get_capture(&file))
        return false;
    std::string text = emit_out(file);
    const std::string path = autosave_path_for(file.scene);
    if (g_snapshot_ready && text == g_snapshot && path == g_snapshot_path.data())
        return true;
    g_snapshot_ready = false; // (the crash hook leaves it alone while it changes)
    g_snapshot.swap(text);
    strncpy_s(g_snapshot_path.data(), g_snapshot_path.size(), path.c_str(), _TRUNCATE);
    strncpy_s(g_snapshot_folder.data(), g_snapshot_folder.size(), get_folder().c_str(), _TRUNCATE);
    g_snapshot_written = false;
    g_snapshot_ready = true;
    return true;
}

bool write_autosave()
{
    if (!g_snapshot_ready || g_snapshot_written || g_offer_pending)
        return false;
    if (!write_whole(g_snapshot_folder.data(), g_snapshot_path.data(), g_snapshot.data(), g_snapshot.size())) {
        get_log("level editor: couldn't write the autosave %s", g_snapshot_path.data());
        return false;
    }
    g_snapshot_written = true;
    g_autosave_ours = true;
    get_log("level editor: autosaved - %s", g_snapshot_path.data());
    return true;
}

void write_autosave_from_crash()
{
    static volatile LONG busy = 0;
    if (!g_snapshot_ready || g_snapshot_written || g_offer_pending || InterlockedExchange(&busy, 1))
        return;
    run_guarded(write_snapshot);
    get_log(g_snapshot_written ? "level editor: the unsaved changes were autosaved - %s (Ctrl+R restores them)"
                               : "level editor: the unsaved changes couldn't be autosaved - %s",
            g_snapshot_path.data());
    busy = 0;
}

bool autosave_offered()
{
    render::WorldScene* scene = scenery::main_scene();
    if (render::current_level() == 0 || !scene || g_autosave_ours || get_refused())
        return false;
    const uint32_t name = scenery::world_scene_key(scene);
    uint64_t autosave_time = 0, saved_time = 0;
    const bool have_autosave = write_time(autosave_path_for(name), &autosave_time);
    const bool have_saved = write_time(path_for(name), &saved_time);
    g_offer_pending = autosave_newer(have_autosave, autosave_time, have_saved, saved_time);
    if (g_offer_pending)
        get_log("level editor: an autosave newer than the saved layout - %s (Ctrl+R restores, Ctrl+Shift+R discards)",
                autosave_path_for(name).c_str());
    return g_offer_pending;
}

bool restore_autosave(std::string* message)
{
    render::WorldScene* scene = scenery::main_scene();
    if (render::current_level() == 0 || !scene) {
        *message = "Not in a level";
        return false;
    }
    const uint32_t name = scenery::world_scene_key(scene);
    const std::string path = autosave_path_for(name);
    FileLoad file;
    if (!read_for(path, name, &file, message, "No autosave for this level"))
        return false;
    reset_to_level(file);
    *message = "Autosave restored: " + get_apply(file);
    get_log("level editor: %s - %s", message->c_str(), path.c_str());
    g_offer_pending = false;
    g_autosave_ours = true; // (it's this run's now: the next autosave goes over it)
    return true;
}

bool discard_autosave(std::string* message)
{
    render::WorldScene* scene = scenery::main_scene();
    if (render::current_level() == 0 || !scene) {
        *message = "Not in a level";
        return false;
    }
    const std::string path = autosave_path_for(scenery::world_scene_key(scene));
    g_offer_pending = false;
    if (!DeleteFileA(path.c_str())) {
        *message = "No autosave to discard";
        return false;
    }
    g_autosave_ours = false;
    *message = "Autosave discarded";
    get_log("level editor: autosave discarded - %s", path.c_str());
    return true;
}

int level_loads() { return g_level_loads; }

namespace {

// A level of its own from now on (an in-place switch between shipped and edited): the editor's unsaved changes and
// the autosave state start again, as for a new level.
void forget_session()
{
    ++g_level_loads;
    g_snapshot_ready = false;
    g_snapshot_written = true;
    g_autosave_ours = false;
    g_offer_pending = false;
}

// The level picked is the one loaded (the level select doesn't load it again): switched to its edited copy (the layout
// put in place) or back to the level as shipped (everything put back as the level has it).
void switch_in_place(int to)
{
    if (get_refused())
        return;
    std::string message;
    if (to > 0 && !g_edited_copy) {
        g_edited_copy = true;
        career::get_edited_copy(true); // (the career's gap list kept as it is first - level_editor_career.h)
        read_in(&message);
        get_log("level editor: switched to the edited copy in place (%s)", message.c_str());
    } else if (to < 0 && g_edited_copy) {
        reset_to_level(FileLoad{});
        g_edited_copy = false;
        career::get_edited_copy(false); // (the career's gap list put back as before)
        get_log("level editor: switched back to the level as shipped in place");
    } else {
        return;
    }
    forget_session();
}

// The level structures (levels.qb's Level_<x> - load_script=Load_<x>, level="<scene>", name="<title>" - and the
// ported levels', which make_menu_level adds the same way): by load script, the scene and the title.
struct LevelInfo {
    uint32_t scene;
    std::string title;
    script::Struct* structure = nullptr; // (phase 6: its level properties go in here - level_editor_level_props.h)
    uint32_t node_array_ = 0;            // (its qb='s node array: level_nodes::node_array_checksum)
};
std::unordered_map<uint32_t, LevelInfo> levels_by_load()
{
    static const uint32_t kLoadScript = checksums::hash_from_text("load_script"),
                          kLevel = checksums::hash_from_text("level"), kName = checksums::hash_from_text("name"),
                          kQb = checksums::hash_from_text("qb");
    std::unordered_map<uint32_t, LevelInfo> levels;
    for (script::SymbolTableEntry* e = script::get_next_symbol(nullptr); e; e = script::get_next_symbol(e)) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
        if (e->type != script::kQbvalStruct || !e->structure)
            continue;
        uint32_t load = 0;
        const char* level = nullptr;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
        if (!e->structure->read_hash_value(kLoadScript, &load, 0) || !e->structure->fetch_text(kLevel, &level, 0))
            continue;
        const char* title = nullptr;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
        e->structure->fetch_text(kName, &title, 0);
        const char* qb = nullptr;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
        e->structure->fetch_text(kQb, &qb, 0);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
        levels.emplace(load, LevelInfo{scene_of_level_text(level), title ? title : "", e->structure,
                                       qb && *qb ? level_nodes::node_array_checksum(qb) : 0});
    }
    return levels;
}

// The scenes with a saved layout (the folder's "<8 hex digits>.txt").
std::vector<uint32_t> saved_scenes()
{
    std::vector<uint32_t> scenes;
    WIN32_FIND_DATAA found;
    HANDLE find = FindFirstFileA((get_folder() + "\\*.txt").c_str(), &found);
    if (find == INVALID_HANDLE_VALUE)
        return scenes;
    do {
        uint32_t scene = 0;
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && saved_layout_scene(std::data(found.cFileName), &scene))
            scenes.push_back(scene);
    } while (FindNextFileA(find, &found));
    FindClose(find);
    return scenes;
}

} // namespace

void on_scene_loaded(const char* name, bool is_net)
{
    (void)name;
    // (a new level: the copies and moved objects of the last are gone with it - and its autosave was written when the
    // editor was left)
    scenery::get_clones().clear();
    object_edit::forget_level();
    node_edit::forget_level();
    fields::forget_level();
    level_props::forget_level();
    faces::forget_level(); // (map3: nothing written - the level's collision and trick objects went with it)
    clusters::forget_level();
    cameras::forget_level();
    goal_edit::forget_level(); // (a goal of ours still in the goal manager taken out: its nodes went with the level)
    links::forget_level();     // (UX7)
    notes::forget_level();     // (UX5)
    checks::forget_level();    // (UX5: the issues, the ignored list, the unresolved records)
    chains::forget_level(); // (before the overlay goes: the level's links put back while its array is the one loaded)
    node_create::forget_level();
    entities::forget_level();
    picking::clear_locks();
    reset_extensions();
    g_pending = true; // (Tick decides: the edited copy if it was picked, never in a network session)
    g_wait = 0;
    ++g_level_loads;
    g_snapshot_ready = false;
    g_snapshot_written = true;
    g_autosave_ours = false;
    g_offer_pending = false;
    g_edited_copy = false;
    career::get_edited_copy(false); // (the career's gap list put back as before the edited copy)
    g_net_scene = is_net;
    g_switch = 0;
}

void tick()
{
    const bool up = render::current_level() != 0 && objects::find_object(kSkater) && scenery::main_scene();
    if (g_switch && up && ++g_switch_wait >= kSettleFrames) {
        const int to = g_switch;
        g_switch = 0;
        switch_in_place(to);
    }
    // The mode became a career-type one with the edited copy loaded (the level not loaded again): back to the level as
    // shipped at once - nothing done from now on is done in the edited copy. (Not while the editor runs: the mode
    // can't change then; it's put back once the editor is left.)
    if (g_edited_copy && up && !live_now()) {
        if (const char* mode = career_mode_active()) {
            reset_to_level(FileLoad{});
            g_edited_copy = false;
            g_request = 0;
            g_pending = false;
            g_switch = 0;
            career::get_edited_copy(false); // (the career's gap list put back as before)
            forget_session();
            get_log("level editor: the game mode is now %s - the edited copy put back as the level as shipped (edited "
                    "levels are free skate / single session only)",
                    mode);
            return;
        }
    }
    if (!g_pending || !up)
        return;
    if (++g_wait < kSettleFrames)
        return;
    g_pending = false;
    // The level as shipped, unless it was picked as its edited copy (and never in a network session or a career-type
    // mode).
    std::string why;
    const bool refused = get_refused(&why);
    const uint32_t load = current_load();
    const bool apply = apply_edited_copy(g_request, load, refused);
    g_request = request_after_load(g_request, load, refused);
    // (map4: the level structure's own ambient light / fog - a bake's ed_ keys, or the edited copy's written there for
    // this load - taken before it's put back; never in a network session)
    level_props::on_level_up(network_active());
    // (the level's structure as the level has it again: load_level has read the edited copy's properties by now)
    level_props::restore_structure();
    if (!apply) {
        if (has_saved_layout())
            get_log("level editor: %s loaded as shipped%s - its edited copy is the level select's (EDITED) entry",
                    level_title().c_str(), refused ? (" (" + why + ")").c_str() : "");
        return;
    }
    g_edited_copy = true;
    career::get_edited_copy(true); // (the career's gap list kept as it is first - level_editor_career.h)
    std::string message;
    read_in(&message);
    level_props::on_edited_copy_up(level_props::at_hand()); // (its start, if it names one)
}

void on_node_array_added(uint32_t array)
{
    if (!g_request || get_refused())
        return;
    // (the level picked as its edited copy - its level structure names its node array (qb=) and its scene (level=))
    std::unordered_map<uint32_t, LevelInfo> levels = levels_by_load();
    auto level = levels.find(g_request);
    if (level == levels.end() || !level->second.node_array_ || level->second.node_array_ != array)
        return;
    FileLoad file;
    std::string message;
    if (!read_for(path_for(level->second.scene), level->second.scene, &file, &message, "No saved layout"))
        return;
    std::vector<fields::NodeEdits> edits;
    for (const SetRecord& record : file.nodes) {
        if (!record.fields.empty())
            edits.push_back({record.name, record.fields});
    }
    if (edits.empty())
        return;
    const int applied = fields::apply_early(array, edits);
    get_log(
        "level editor: the edited copy's node array %s loaded - %d field edits written into its nodes before they're "
        "made",
        object_edit::caption(array).c_str(), applied);
}

bool saved_props(uint32_t scene, level_props::Props* props)
{
    FileLoad file;
    const std::ifstream in(path_for(scene), std::ios::binary);
    if (!in)
        return false;
    std::stringstream text;
    text << in.rdbuf();
    int line = 0;
    if (!get_take_in(text.str(), &file, &line) || file.scene != scene)
        return false;
    *props = file.level;
    return !level_props::vacant(file.level);
}

bool network_active()
{
    net::Hub* net = net::Hub::sole();
    return network_session(net && net->is_online(), in_net_game(), g_net_scene);
}

ModeFlags current_mode()
{
    // (gamemode.qb's property names; is_career and is_classic as IsCareerMode 0x005AEFC0 / the goal win 0x005576D0 ask)
    static const uint32_t kIsFreeskate = 0x337E7779, kIsSingleSession = checksums::hash_from_text("is_singlesession"),
                          kIsCareer = 0x1DED1EA4, kIsClassic = 0x5A2C589A,
                          kIsCreateGoals = checksums::hash_from_text("is_creategoals"),
                          kIsParkEditor = checksums::hash_from_text("is_parkeditor"),
                          kIsNet = checksums::hash_from_text("is_net");
    ModeFlags flags;
    goals::MatchRules* mode = goals::g_game_mode;
    if (!mode)
        return flags;
    // (0x00553220: the mode's structure's integer `property`, non-zero)
    auto is = [mode](uint32_t property) { return mode->is_true(property); };
    flags.known = true;
    flags.freeskate = is(kIsFreeskate);
    flags.single_session_ = is(kIsSingleSession);
    flags.career = is(kIsCareer);
    flags.classic = is(kIsClassic);
    flags.create_goals_ = is(kIsCreateGoals);
    flags.park_editor_ = is(kIsParkEditor);
    flags.net = is(kIsNet);
    return flags;
}

const char* career_mode_active() { return mode_refusal(current_mode()); }

bool get_refused(std::string* why)
{
    std::string reason;
    if (network_active())
        reason = "a network session: layouts are offline only";
    else if (const char* mode = career_mode_active())
        reason = std::string(mode) + ": edited levels are free skate / single session only";
    if (why)
        *why = reason;
    return !reason.empty();
}

void panel_message(const char* text)
{
    static const uint32_t kScript = checksums::hash_from_text("create_panel_message");
    if (!text || !*text || render::current_level() == 0 || !script::script_exists(kScript))
        return;
    auto* params = new (script::spawn_param_bag()) script::Struct;
    params->append_word(checksums::hash_from_text("text"), text);
    params->put_name_key(checksums::hash_from_text("id"), checksums::hash_from_text("level_editor_refused_message"));
    params->insert_pair(checksums::hash_from_text("pos"), 320.0f, 70.0f);
    params->put_int(checksums::hash_from_text("time"), 5000);
    script::spawn_script(kScript, params, 0, nullptr, 0, 0, false, false, false, false);
    params->get_clear();
    script::delete_struct(params);
}

bool editing_copy() { return g_edited_copy; }

bool has_saved_layout()
{
    render::WorldScene* scene = scenery::main_scene();
    uint64_t time = 0;
    return render::current_level() != 0 && scene && write_time(path_for(scenery::world_scene_key(scene)), &time);
}

std::string level_title()
{
    // (phase 6: the edited copy's own name, as its level properties give it)
    if (g_edited_copy && !level_props::at_hand().name.empty())
        return level_props::at_hand().name;
    // (the symbols walked once a level: the toolbar asks every frame)
    static uint32_t g_cached_load = 0, g_cached_scene = 0;
    static std::string cached;
    const uint32_t load = current_load();
    const uint32_t scene = scenery::world_scene_key(scenery::main_scene());
    if (load != g_cached_load || scene != g_cached_scene || cached.empty()) {
        g_cached_load = load;
        g_cached_scene = scene;
        std::unordered_map<uint32_t, LevelInfo> levels = levels_by_load();
        auto it = levels.find(load);
        cached = it != levels.end() && !it->second.title.empty() ? it->second.title
                 : scene                                         ? object_edit::caption(scene)
                                                                 : "";
    }
    return cached;
}

bool enter_edited_copy(std::string* message)
{
    if (render::current_level() == 0 || !scenery::main_scene()) {
        *message = "Not in a level";
        return false;
    }
    if (network_active()) {
        *message = "Level editor is not available online";
        return false;
    }
    if (const char* mode = career_mode_active()) {
        *message = std::string("The level editor isn't offered in ") + mode + " (free skate and single session only)";
        return false;
    }
    g_switch = 0;
    g_request = current_load(); // (loaded again, it stays the edited copy)
    if (g_edited_copy) {
        message->clear();
        return true;
    }
    g_edited_copy = true;
    career::get_edited_copy(true); // (the career's gap list kept as it is first - level_editor_career.h)
    g_pending = false;             // (put in place here, not again by Tick)
    std::string loaded;
    if (has_saved_layout() && read_in(&loaded))
        *message = "Editing " + level_title() + " (Edited)";
    else
        *message = "A new edited copy of " + level_title() + " - made by the first save (Ctrl+S)";
    get_log("level editor: %s (%s)", message->c_str(), loaded.c_str());
    return true;
}

bool reset_level(std::string* before, std::string* message)
{
    FileLoad file;
    if (!get_capture(&file)) {
        *message = "Not in a level";
        return false;
    }
    *before = emit_out(file);
    reset_to_level(FileLoad{});
    // (the edited copy's files: the saved layout - its level select entry - and the autosave)
    int deleted = 0;
    for (const std::string& path : {path_for(file.scene), autosave_path_for(file.scene)}) {
        if (DeleteFileA(path.c_str())) {
            ++deleted;
            get_log("level editor: reset - deleted %s", path.c_str());
        }
    }
    g_snapshot_ready = false;
    g_snapshot_written = true;
    g_autosave_ours = false;
    g_offer_pending = false;
    *message = deleted ? "Level reset; saved layout deleted" : "Level reset";
    get_log("level editor: %s", message->c_str());
    return true;
}

bool capture_text(std::string* text)
{
    FileLoad file;
    if (!get_capture(&file))
        return false;
    *text = emit_out(file);
    return true;
}

bool put_back(const std::string& before, std::string* message)
{
    FileLoad file;
    int line = 0;
    render::WorldScene* scene = scenery::main_scene();
    if (render::current_level() == 0 || !scene || !get_take_in(before, &file, &line) ||
        file.scene != scenery::world_scene_key(scene)) {
        *message = "Couldn't restore the changes";
        return false;
    }
    reset_to_level(file);
    const std::string summary = get_apply(file);
    *message = "Reset undone (unsaved)";
    get_log("level editor: %s - %s", message->c_str(), summary.c_str());
    return true;
}

bool cfunc_pick_level(script::Struct* params, script::Script*)
{
    static const uint32_t kLevel = checksums::hash_from_text("level"),
                          kEditedLayout = checksums::hash_from_text("edited_layout");
    uint32_t load = 0;
    if (params)
        params->read_hash_value(kLevel, &load, 0);
    const bool edited = params && params->has_bare_flag(kEditedLayout);
    // (never in a network session or a career-type mode: the level as shipped)
    std::string why;
    const bool network = get_refused(&why);
    if (edited && network)
        get_log("level editor: an edited copy picked - refused, the level as shipped (%s)", why.c_str());
    g_request = request_from_pick(load, edited, network);
    // (phase 6) the edited copy's field edits go into its node array as it loads; its level properties into its level
    // structure now, before load_level reads it (put back once the level is up - Tick - or another level is picked)
    level_nodes::set_node_array_added_hook(&on_node_array_added);
    level_props::restore_structure();
    const bool loaded_already = load && load == current_load() && render::current_level() != 0;
    if (g_request && !loaded_already) {
        std::unordered_map<uint32_t, LevelInfo> levels = levels_by_load();
        auto level = levels.find(g_request);
        level_props::Props props;
        if (level != levels.end() && saved_props(level->second.scene, &props))
            level_props::export_to_struct(level->second.structure, props);
    }
    // (the level already loaded: the level select won't load it again - switched in place once its menus are gone)
    if (loaded_already && !network && (g_request != 0) != g_edited_copy) {
        g_switch = g_request ? 1 : -1;
        g_switch_wait = 0;
    }
    get_log("level editor: level picked - %s%s", object_edit::caption(load).c_str(),
            g_request ? " (its edited copy)" : " (as shipped)");
    return true;
}

bool cfunc_edited_levels(script::Struct*, script::Script* script)
{
    if (!script || !script->args_ || get_refused())
        return false;
    std::vector<uint32_t> saved = saved_scenes();
    if (saved.empty())
        return false;
    static const uint32_t kLevel = checksums::hash_from_text("level"), kText = checksums::hash_from_text("text"),
                          kEditedLayout = checksums::hash_from_text("edited_layout"),
                          kSignLabel = checksums::hash_from_text("sign_label");
    static const auto kLists = std::to_array<const char*>({"level_select_menu_level_info",       "level_select_menu_thug1_level_info",
                                         "level_select_menu_thps4_level_info", "level_select_menu_thps3_level_info",
                                         "level_select_menu_thaw_level_info",  "level_select_menu_misc_level_info"});
    static const auto kDropped = std::to_array<const char*>({"new_vmenu", "hold_vmenu"}); // (columns of the level's own section)
    std::unordered_map<uint32_t, LevelInfo> levels = levels_by_load();
    std::vector<script::Struct*> entries;
    std::vector<uint32_t> done;
    for (const char* list : kLists) {
        script::Array* array = script::fetch_list(list);
        if (!array || array->kind_tag_ != script::kQbvalStruct)
            continue;
        for (uint32_t i = 0; i < array->extent_; ++i) {
            auto* entry = array->struct_at(i);
            uint32_t load = 0;
            if (!entry || !entry->read_hash_value(kLevel, &load, 0))
                continue;
            auto level = levels.find(load);
            if (level == levels.end() || std::ranges::find(saved, level->second.scene) == saved.end() ||
                std::ranges::find(done, level->second.scene) != done.end())
                continue;
            done.push_back(level->second.scene);
            const char* text = nullptr;
            entry->fetch_text(kText, &text, 0);
            // (phase 6 / map4: the edited copy's own name, when its level properties give it one - its title, and the
            // label the overlay's level select draws on its sign: sign_label - tools/pipeline/menu_level/level_select.ts)
            level_props::Props props;
            std::string named;
            if (saved_props(level->second.scene, &props))
                named = props.name;
            auto* copy = new (script::spawn_param_bag()) script::Struct;
            copy->merge_structure(entry);
            for (const char* flag : kDropped)
                copy->remove_flag(checksums::hash_from_text(flag));
            copy->append_word(kText, edited_title(text, named).c_str());
            copy->append_word(kSignLabel, sign_label(named).c_str());
            copy->put_name_key(0u, kEditedLayout);
            entries.push_back(copy);
        }
    }
    if (entries.empty())
        return false;
    auto* array = new (script::alloc_script_array()) script::Array;
    array->configure_size_kind(static_cast<uint32_t>(entries.size()), script::kQbvalStruct);
    for (size_t i = 0; i < entries.size(); ++i)
        array->set_element(static_cast<uint32_t>(i), cell::of(core::at(entries, i)));
    script->args_->append_array_ref("edited_list", array);
    script->args_->put_int("edited_count", static_cast<int>(entries.size()));
    return true;
}

} // namespace frontend::level_editor::layout
