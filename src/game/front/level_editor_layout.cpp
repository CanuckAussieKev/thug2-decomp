// NOT ORIGINAL - the level editor's saved layouts (ours). See level_editor_layout.h.

#include "game/front/level_editor_layout.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "game/core/crc.h"
#include "game/front/level_editor_career.h"
#include "game/front/level_editor_node_edit.h"
#include "game/front/level_editor_objects.h"
#include "game/front/level_editor_scale.h"
#include "game/gfx/screen.h"
#include "game/net/net_manager.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/node_manager.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/log.h"

namespace Front::LevelEditor::Layout {

// --- the text (pure) --------------------------------------------------------------------------------------------

namespace {

const char kHeader[] = "thug2-decomp layout "; // (and the version)

// The header's version (0: not a layout's header).
int HeaderVersion(const std::string& line)
{
    if (line.rfind(kHeader, 0) != 0)
        return 0;
    std::istringstream in(line.substr(sizeof(kHeader) - 1));
    int version = 0;
    std::string rest;
    if (!(in >> version) || version < 1 || (in >> rest))
        return 0;
    return version;
}

// "x,y,z" (three numbers, commas between).
bool ReadTriple(const std::string& text, float out[3])
{
    const char* at = text.c_str();
    for (int i = 0; i < 3; ++i) {
        char* end = nullptr;
        out[i] = std::strtof(at, &end);
        if (end == at || !std::isfinite(out[i]))
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

void Triple(std::ostringstream& out, const char* key, const float value[3])
{
    char text[96];
    std::snprintf(text, sizeof(text), " %s=%.9g,%.9g,%.9g", key, value[0], value[1], value[2]);
    out << text;
}

void Number(std::ostringstream& out, float value)
{
    char text[32];
    std::snprintf(text, sizeof(text), " %.9g", value);
    out << text;
}

void Hex(std::ostringstream& out, uint32_t value)
{
    char text[16];
    std::snprintf(text, sizeof(text), " %08X", value);
    out << text;
}

void Placed(std::ostringstream& out, const Placement& p)
{
    out << ' ' << p.quarters;
    Number(out, p.offset.x);
    Number(out, p.offset.y);
    Number(out, p.offset.z);
}

void Comment(std::ostringstream& out, uint32_t checksum)
{
    if (const char* name = Objects::NameOf(checksum))
        out << "  # " << name;
    out << '\n';
}

bool ReadHex(std::istringstream& in, uint32_t* value)
{
    std::string word;
    if (!(in >> word) || word.empty() || word.size() > 8)
        return false;
    char* end = nullptr;
    *value = static_cast<uint32_t>(std::strtoul(word.c_str(), &end, 16));
    return end && *end == '\0';
}

bool ReadPlaced(std::istringstream& in, Placement* p)
{
    return static_cast<bool>(in >> p->quarters >> p->offset.x >> p->offset.y >> p->offset.z) && p->quarters >= 0 &&
           p->quarters <= 3;
}

// The key=value fields after a sector / rail / clone / object / copy record's numbers: scale= (level_editor_scale.h);
// others (a later version's) skipped. False: a bad scale.
bool ReadScaleFields(std::istringstream& in, V3* scale)
{
    std::string field;
    while (in >> field) {
        if (field.compare(0, 6, "scale=") == 0) {
            if (!Scale::ReadToken(field, scale))
                return false;
        }
    }
    return true;
}
bool ReadPlacedFields(std::istringstream& in, Placement* p) { return ReadScaleFields(in, &p->scale); }

// A `node` record's fields (after "node"): name, class, then key=value (unknown keys skipped; a repeated one, a bad
// value or a missing place refuse it).
bool ReadNode(std::istringstream& in, NodeCreate::Spec* spec)
{
    if (!(in >> spec->name >> spec->cls) || !NodeCreate::ValidName(spec->name) || !NodeCreate::ValidName(spec->cls))
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
        float v[3];
        char* end = nullptr;
        if (key == "type") {
            if (!NodeCreate::ValidName(value))
                return false;
            spec->type = value;
        } else if (key == "pos" || key == "angles") {
            if (!ReadTriple(value, v))
                return false;
            (key == "pos" ? spec->pos : spec->angles) = {v[0], v[1], v[2]};
        } else if (key == "types") {
            std::istringstream list(value);
            std::string type;
            while (std::getline(list, type, ',')) {
                if (!NodeCreate::ValidName(type))
                    return false;
                spec->types.push_back(type);
            }
        } else if (key == "color") {
            if (!ReadTriple(value, v))
                return false;
            for (int i = 0; i < 3; ++i) {
                if (v[i] < 0.0f || v[i] > 255.0f || v[i] != static_cast<float>(static_cast<int>(v[i])))
                    return false;
                spec->colour[i] = static_cast<int>(v[i]);
            }
        } else if (key == "brightness" || key == "inner" || key == "outer" || key == "radius") {
            float number = std::strtof(value.c_str(), &end);
            if (value.empty() || *end != '\0' || !std::isfinite(number))
                return false;
            (key == "brightness" ? spec->brightness : key == "inner" ? spec->inner : key == "outer" ? spec->outer
                                                                                                    : spec->radius) = number;
        }
    }
    return std::find(seen.begin(), seen.end(), "pos") != seen.end();
}

} // namespace

std::string Write(const File& file)
{
    std::ostringstream out;
    out << kHeader << kVersion << '\n';
    out << "scene";
    Hex(out, file.scene);
    Comment(out, file.scene);
    out << LevelProps::WriteRecord(file.level);
    for (const auto& s : file.sectors) {
        out << "sector";
        Hex(out, s.checksum);
        Placed(out, s.placement);
        out << ' ' << (s.hidden ? 1 : 0) << Scale::Token(s.placement.scale);
        Comment(out, s.checksum);
    }
    for (const auto& r : file.rails) {
        out << "rail";
        Hex(out, r.name);
        Placed(out, r.placement);
        out << ' ' << (r.hidden ? 1 : 0) << Scale::Token(r.placement.scale);
        Comment(out, r.name);
    }
    for (const auto& c : file.clones) {
        out << "clone";
        Hex(out, c.source);
        Placed(out, c.placement);
        out << Scale::Token(c.placement.scale);
        Comment(out, c.source);
    }
    for (const auto& o : file.objects) {
        out << "object";
        Hex(out, o.id);
        for (float v : o.pos)
            Number(out, v);
        for (float v : o.rows)
            Number(out, v);
        out << Scale::Token(o.scale);
        Comment(out, o.id);
    }
    for (const auto& c : file.copies) {
        out << "copy";
        Hex(out, c.name);
        Hex(out, c.source);
        for (float v : c.pos)
            Number(out, v);
        for (float v : c.rows)
            Number(out, v);
        out << Scale::Token(c.scale);
        Comment(out, c.source);
    }
    for (const NodeCreate::Spec& a : file.added) {
        out << "node " << a.name << ' ' << a.cls;
        if (!a.type.empty())
            out << " type=" << a.type;
        const float pos[3] = {a.pos.x, a.pos.y, a.pos.z}, angles[3] = {a.angles.x, a.angles.y, a.angles.z};
        Triple(out, "pos", pos);
        Triple(out, "angles", angles);
        if (!a.types.empty()) {
            out << " types=";
            for (size_t i = 0; i < a.types.size(); ++i)
                out << (i ? "," : "") << a.types[i];
        }
        NodeCreate::Kind kind;
        if (NodeCreate::KindOf(a, &kind) && kind == NodeCreate::Kind::Light) {
            out << " color=" << a.colour[0] << ',' << a.colour[1] << ',' << a.colour[2];
            char text[96];
            std::snprintf(text, sizeof(text), " brightness=%.9g inner=%.9g outer=%.9g", a.brightness, a.inner, a.outer);
            out << text;
        }
        if (a.radius != 0.0f) {
            char text[32];
            std::snprintf(text, sizeof(text), " radius=%.9g", a.radius);
            out << text;
        }
        out << '\n';
    }
    if (!file.crownOrder.empty()) {
        out << "order crown";
        for (const std::string& name : file.crownOrder)
            out << ' ' << name;
        out << '\n';
    }
    for (uint32_t name : file.hidden) {
        out << "hide";
        Hex(out, name);
        Comment(out, name);
    }
    for (const Picking::Lock& lock : file.locks) {
        out << (lock.kind == Picking::kObjects ? "lock object" : "lock sector");
        Hex(out, lock.id);
        Comment(out, lock.id);
    }
    for (const Triggers::Placed& t : file.triggers)
        out << "trigger " << Triggers::Record(t) << '\n';
    for (const SetRecord& s : file.nodes) {
        if (!s.hasPos && !s.hasAngles && s.fields.empty())
            continue;
        out << "set";
        Hex(out, s.name);
        if (s.hasPos)
            Triple(out, "pos", s.pos);
        if (s.hasAngles)
            Triple(out, "angles", s.angles);
        for (const Fields::Edit& edit : s.fields)
            out << ' ' << Fields::Token(edit, Fields::NameOf);
        Comment(out, s.name);
    }
    out << Chains::WriteRecords(file.chains);
    for (const Walls::WallState& w : file.walls) {
        out << "wall";
        Hex(out, w.checksum);
        Placed(out, w.placement);
        out << ' ' << (w.off ? 1 : 0);
        Comment(out, w.checksum);
    }
    return out.str();
}

bool Read(const std::string& text, File* file, int* lineNumber)
{
    *file = File{};
    std::istringstream lines(text);
    std::string line;
    int number = 0;
    bool header = false;
    while (std::getline(lines, line)) {
        ++number;
        *lineNumber = number;
        if (size_t hash = line.find('#'); hash != std::string::npos)
            line.resize(hash);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::istringstream in(line);
        std::string kind;
        if (!(in >> kind))
            continue;
        if (!header) {
            file->version = HeaderVersion(line);
            if (!file->version)
                return false;
            header = true;
            continue;
        }
        bool ok = true;
        if (kind == "scene") {
            ok = ReadHex(in, &file->scene);
        } else if (kind == "sector" || kind == "rail") {
            uint32_t checksum = 0;
            Placement p = {};
            int hidden = 0;
            ok = ReadHex(in, &checksum) && ReadPlaced(in, &p) && (in >> hidden) && ReadPlacedFields(in, &p);
            if (ok && kind == "sector")
                file->sectors.push_back({checksum, p, hidden != 0});
            else if (ok)
                file->rails.push_back({checksum, p, hidden != 0});
        } else if (kind == "clone") {
            CloneRecord c = {};
            ok = ReadHex(in, &c.source) && ReadPlaced(in, &c.placement) && ReadPlacedFields(in, &c.placement);
            if (ok)
                file->clones.push_back(c);
        } else if (kind == "object") {
            ObjectRecord o = {};
            ok = ReadHex(in, &o.id);
            for (float& v : o.pos)
                ok = ok && (in >> v);
            for (float& v : o.rows)
                ok = ok && (in >> v);
            ok = ok && ReadScaleFields(in, &o.scale);
            if (ok)
                file->objects.push_back(o);
        } else if (kind == "copy") {
            CopyRecord c = {};
            ok = ReadHex(in, &c.name) && ReadHex(in, &c.source);
            for (float& v : c.pos)
                ok = ok && (in >> v);
            for (float& v : c.rows)
                ok = ok && (in >> v);
            ok = ok && ReadScaleFields(in, &c.scale);
            if (ok)
                file->copies.push_back(c);
        } else if (kind == "set") {
            // (key=value fields: pos and angles; others - a later version's - skipped)
            SetRecord s = {};
            ok = ReadHex(in, &s.name);
            std::string field;
            while (ok && in >> field) {
                size_t equals = field.find('=');
                if (equals == std::string::npos || equals == 0) {
                    ok = false;
                    break;
                }
                std::string key = field.substr(0, equals), value = field.substr(equals + 1);
                if (key == "pos") {
                    ok = !s.hasPos && ReadTriple(value, s.pos); // (twice: not a record we wrote)
                    s.hasPos = true;
                } else if (key == "angles") {
                    ok = !s.hasAngles && ReadTriple(value, s.angles);
                    s.hasAngles = true;
                } else if (Fields::IsFieldKey(key)) {
                    // (phase 6: a typed field edit; the same field twice isn't a record we wrote)
                    Fields::Edit edit;
                    ok = Fields::ReadToken(key, value, &edit);
                    for (const Fields::Edit& other : s.fields)
                        ok = ok && other.key != edit.key;
                    if (ok)
                        s.fields.push_back(edit);
                }
            }
            if (ok)
                file->nodes.push_back(s);
        } else if (kind == "node") {
            ok = ReadNode(in, &file->added.emplace_back());
        } else if (kind == "order") {
            std::string what, name;
            ok = static_cast<bool>(in >> what);
            if (ok && what == "crown") { // (an order of something else: a later version's - skipped)
                file->crownOrder.clear();
                while (ok && in >> name) {
                    ok = NodeCreate::ValidName(name);
                    file->crownOrder.push_back(name);
                }
            }
        } else if (kind == "hide") {
            uint32_t name = 0;
            ok = ReadHex(in, &name);
            if (ok)
                file->hidden.push_back(name);
        } else if (kind == "lock") {
            std::string what;
            uint32_t id = 0;
            ok = (in >> what) && ReadHex(in, &id);
            if (ok && (what == "object" || what == "sector"))
                file->locks.push_back({what == "object" ? Picking::kObjects : Picking::kScenery, id});
        } else if (kind == "trigger") {
            std::string rest;
            std::getline(in, rest);
            ok = Triggers::ReadRecord(rest.substr(std::min(rest.size(), rest.find_first_not_of(' '))),
                                      &file->triggers.emplace_back());
        } else if (kind == "wall") {
            // (level_editor_walls.h: later key=value fields skipped, as a sector's)
            Walls::WallState w = {};
            int off = 0;
            ok = ReadHex(in, &w.checksum) && ReadPlaced(in, &w.placement) && (in >> off) && (off == 0 || off == 1);
            w.off = off != 0;
            w.placement.scale = {1.0f, 1.0f, 1.0f};
            if (ok)
                file->walls.push_back(w);
        } else if (Chains::IsRecord(kind)) {
            ok = Chains::ReadRecord(kind, in, &file->chains); // (phase 4: point / links / drop / terrain)
        } else if (kind == "level") {
            ok = LevelProps::ReadRecord(in, &file->level); // (phase 6: the level's properties)
        } // (anything else: a later version's record - skipped)
        if (!ok)
            return false;
    }
    *lineNumber = 0;
    return header;
}

// --- the autosave's names and times (pure) ---------------------------------------------------------------------

std::string SavedFileName(uint32_t scene)
{
    char name[32];
    std::snprintf(name, sizeof(name), "%08X.txt", scene);
    return name;
}

std::string AutosaveFileName(uint32_t scene)
{
    char name[32];
    std::snprintf(name, sizeof(name), "%08X.autosave.txt", scene);
    return name;
}

bool AutosaveNewer(bool haveAutosave, uint64_t autosaveTime, bool haveSaved, uint64_t savedTime)
{
    return haveAutosave && (!haveSaved || autosaveTime > savedTime);
}

// --- the edited copy (pure) --------------------------------------------------------------------------------------

bool ApplyEditedCopy(uint32_t request, uint32_t levelLoad, bool network)
{
    return !network && request != 0 && request == levelLoad;
}

uint32_t RequestAfterLoad(uint32_t request, uint32_t levelLoad, bool network)
{
    return network || request != levelLoad ? 0 : request;
}

uint32_t RequestFromPick(uint32_t load, bool edited, bool network) { return edited && !network ? load : 0; }

bool NetworkSession(bool online, bool netGame, bool netScene) { return online || netGame || netScene; }

std::string EditedText(const char* text) { return std::string(text && *text ? text : "LEVEL") + " (EDITED)"; }

bool SavedLayoutScene(const std::string& fileName, uint32_t* scene)
{
    // (exactly "XXXXXXXX.txt": "XXXXXXXX.autosave.txt" and "XXXXXXXX.txt.tmp" aren't)
    if (fileName.size() != 12 || _stricmp(fileName.c_str() + 8, ".txt") != 0)
        return false;
    uint32_t value = 0;
    for (int i = 0; i < 8; ++i) {
        char c = fileName[i];
        int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (digit < 0)
            return false;
        value = value << 4 | static_cast<uint32_t>(digit);
    }
    *scene = value;
    return true;
}

uint32_t SceneOfLevelText(const char* level) { return level && *level ? Crc::GenerateCRCFromString(level) : 0; }

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kSkater = 0x5B8AB877; // Skater (the local skater's alias)
constexpr int kSettleFrames = 60;        // (a level's objects and rails are made after its scenes: wait a second)

bool g_pending;
int g_wait;
int g_levelLoads;

// The autosave: the text made by the last Snapshot and where it goes (plain memory the crash hook can write without
// allocating: `ready` is false while they change), whether it has been written since, whether this run wrote (or
// restored) this level's autosave, and whether an older one is being offered (nothing written over it meanwhile).
std::string g_snapshot;
char g_snapshotPath[MAX_PATH];
char g_snapshotFolder[MAX_PATH];
volatile bool g_snapshotReady;
volatile bool g_snapshotWritten = true;
bool g_autosaveOurs;
bool g_offerPending;

// The edited copy: the level picked as edited (its load script; 0: none - LevelEditor_PickLevel), whether the level
// loaded now is its edited copy, whether the last scene was loaded for a network game, and a switch asked for in place
// (the level picked is the one loaded: +1 to the edited copy, -1 back to the level as shipped) and its wait.
uint32_t g_request;
bool g_editedCopy;
bool g_netScene;
int g_switch;
int g_switchWait;

// The network game (GameNet::Manager 0x007CCE14, 0x0044E4F0: a network game is on - host or client).
bool InNetGame()
{
    void* net = GameVariable<void*>(0x007CCE14);
    return net && GameFunction<bool(__thiscall*)(void*)>(0x0044E4F0)(net);
}

// The level loaded: its load script (the skate module 0x007CE478, +0x378 - as automation.h and the node layer read it).
uint32_t CurrentLoad()
{
    uint8_t* skate = GameVariable<uint8_t*>(0x007CE478);
    return skate ? *reinterpret_cast<uint32_t*>(skate + 0x378) : 0;
}

std::string Folder() { return GameFolderPath("thug2-decomp-layouts"); }

std::string PathFor(uint32_t scene) { return Folder() + "\\" + SavedFileName(scene); }
std::string AutosavePathFor(uint32_t scene) { return Folder() + "\\" + AutosaveFileName(scene); }

// A file's last-write time (false: no such file).
bool WriteTime(const std::string& path, uint64_t* time)
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
bool WriteWhole(const char* folder, const char* path, const char* data, size_t size)
{
    char temporary[MAX_PATH + 8];
    if (std::snprintf(temporary, sizeof(temporary), "%s.tmp", path) <= 0)
        return false;
    CreateDirectoryA(folder, nullptr);
    HANDLE file = CreateFileA(temporary, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    bool ok = WriteFile(file, data, static_cast<DWORD>(size), &written, nullptr) && written == size;
    CloseHandle(file);
    ok = ok && MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING);
    if (!ok)
        DeleteFileA(temporary);
    return ok;
}

void RowsOf(const Mth::Matrix& m, float rows[9])
{
    for (int r = 0; r < 3; ++r) {
        rows[r * 3 + 0] = m.row[r].x;
        rows[r * 3 + 1] = m.row[r].y;
        rows[r * 3 + 2] = m.row[r].z;
    }
}

Objects::State StateOf(uint32_t id, const float pos[3], const float rows[9], V3 scale)
{
    Objects::State state = {};
    state.id = id;
    state.scale = scale;
    state.pos = {pos[0], pos[1], pos[2], 1.0f};
    for (int r = 0; r < 3; ++r) {
        state.matrix.row[r] = {rows[r * 3 + 0], rows[r * 3 + 1], rows[r * 3 + 2], 0.0f};
        state.displayMatrix.row[r] = state.matrix.row[r];
    }
    state.matrix.row[3] = state.displayMatrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    return state;
}

Obj::CCompositeObject* LiveObject(uint32_t id)
{
    auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(id));
    return object && !(object->m_objectFlags & Obj::OBJECT_FLAG_DEAD) ? object : nullptr;
}

// The level's changes as they are now (false: not in a level).
bool Capture(File* file)
{
    uint8_t* scene = Scenery::MainScene();
    if (Nx::CurrentLevel() == 0 || !scene)
        return false;
    *file = File{};
    file->scene = Scenery::SceneName(scene);
    file->sectors = Scenery::ChangedSectors();
    file->rails = Scenery::ChangedRails();
    // (a rail point the editor added: its `point` record has where it is now - not a `rail` offset)
    file->rails.erase(std::remove_if(file->rails.begin(), file->rails.end(),
                                     [](const Scenery::RailState& r) { return NodeCreate::IsAdded(r.name); }),
                      file->rails.end());
    for (const Scenery::Clone& clone : Scenery::Clones()) {
        Placement now;
        if (Scenery::PlacementNow(clone.checksum, &now) && !Scenery::Hidden(clone.checksum))
            file->clones.push_back({clone.source, Compose(now, clone.sourceAtCopy)});
    }
    for (uint32_t id : Objects::Touched()) {
        Obj::CCompositeObject* object = LiveObject(id);
        if (!object)
            continue;
        ObjectRecord record = {id, {object->m_pos.x, object->m_pos.y, object->m_pos.z}, {}};
        RowsOf(object->m_matrix, record.rows);
        record.scale = Scale::ObjectScale(object);
        file->objects.push_back(record);
    }
    for (const Objects::Copy& copy : Objects::Copies()) {
        Obj::CCompositeObject* object = LiveObject(copy.name);
        if (!object)
            continue;
        CopyRecord record = {copy.name, copy.source, {object->m_pos.x, object->m_pos.y, object->m_pos.z}, {}};
        RowsOf(object->m_matrix, record.rows);
        record.scale = Scale::ObjectScale(object);
        file->copies.push_back(record);
    }
    // Added nodes (as `node` records, where they are now; the crowns' order is theirs), hidden ones.
    // (rail / path points: the chains' `point` records; the level's points deleted: `drop`, not `hide`)
    file->chains = Chains::Current();
    file->added = NodeCreate::Current();
    // (a trigger volume's node: its own `trigger` record - level_editor_triggers.h)
    file->added.erase(std::remove_if(file->added.begin(), file->added.end(),
                                     [](const NodeCreate::Spec& spec) { return spec.owned; }),
                      file->added.end());
    file->triggers = Triggers::CaptureAdded();
    file->added.erase(std::remove_if(file->added.begin(), file->added.end(),
                                     [](const NodeCreate::Spec& s) { return Chains::IsChainClass(s.cls); }),
                      file->added.end());
    std::vector<std::string> crowns = NodeCreate::CrownOrder(file->added);
    if (crowns.size() > 1)
        file->crownOrder = crowns;
    for (uint32_t name : NodeCreate::HiddenNames()) {
        if (std::find(file->chains.dropped.begin(), file->chains.dropped.end(), name) == file->chains.dropped.end())
            file->hidden.push_back(name);
    }
    file->locks = Picking::Locks();
    file->walls = Walls::Changed();
    for (const NodeEdit::Record& node : NodeEdit::Changed()) {
        if (NodeCreate::IsAdded(node.name))
            continue; // (its node record has its place)
        const NodeEdit::Values& v = node.values;
        file->nodes.push_back({node.name, true, true, {v.pos.x, v.pos.y, v.pos.z}, {v.angles.x, v.angles.y, v.angles.z}});
    }
    // (phase 6) the Inspector's field edits: on the node's set record (one made for a node not moved)
    for (const Fields::NodeEdits& node : Fields::Changed()) {
        auto it = std::find_if(file->nodes.begin(), file->nodes.end(),
                               [&](const SetRecord& s) { return s.name == node.name; });
        if (it == file->nodes.end()) {
            SetRecord record = {};
            record.name = node.name;
            file->nodes.push_back(record);
            it = file->nodes.end() - 1;
        }
        it->fields = node.edits;
    }
    file->level = LevelProps::Current();
    return true;
}

// A layout file of this level read (false: none - `message` - or not one; logged).
bool ReadFor(const std::string& path, uint32_t scene, File* file, std::string* message, const char* none)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        *message = none;
        return false;
    }
    std::stringstream text;
    text << in.rdbuf();
    int line = 0;
    if (!Read(text.str(), file, &line) || file->scene != scene) {
        *message = "Couldn't read " + path.substr(path.find_last_of('\\') + 1);
        Log("level editor: %s %s (line %d)", message->c_str(), path.c_str(), line);
        return false;
    }
    return true;
}

// A layout put in place: copies of sectors made (each of its source as loaded - the sources aren't moved yet - then
// put where they were), sectors and rails placed, objects placed, copies of objects placed (made unless one of that
// name is already there).
std::string Apply(const File& file)
{
    std::vector<Scenery::SectorState> clones;
    for (const CloneRecord& record : file.clones) {
        if (uint32_t made = Scenery::Duplicate(record.source))
            clones.push_back({made, record.placement, false});
    }
    Scenery::ApplyStates(clones, {});
    Scenery::ApplyStates(file.sectors, file.rails);
    int objects = 0, copies = 0;
    for (const ObjectRecord& record : file.objects) {
        if (Objects::Restore(StateOf(record.id, record.pos, record.rows, record.scale))) {
            Objects::Touch(record.id);
            ++objects;
        }
    }
    for (const CopyRecord& record : file.copies) {
        bool there = Objects::IsCopy(record.name) && LiveObject(record.name);
        if ((there || Objects::MakeCopy(record.source, {record.pos[0], record.pos[1], record.pos[2]}, record.name)) &&
            Objects::Restore(StateOf(record.name, record.pos, record.rows, record.scale)))
            ++copies;
    }
    // Nodes added (in the overlay, in order: one re-registration), the added crowns' order, the level's nodes hidden.
    std::string message;
    int added = file.added.empty() ? 0 : NodeCreate::CreateAll(file.added, &message);
    if (!file.crownOrder.empty())
        NodeCreate::ApplyCrownOrder(file.crownOrder);
    int hidden = 0;
    for (uint32_t name : file.hidden) {
        if (NodeCreate::Hide(name, true))
            ++hidden;
    }
    // Level nodes moved / turned (a node the level no longer has is skipped).
    int nodes = 0;
    for (const SetRecord& record : file.nodes) {
        NodeEdit::Values values;
        if (!NodeEdit::Read(record.name, &values))
            continue;
        if (record.hasPos)
            values.pos = {record.pos[0], record.pos[1], record.pos[2]};
        if (record.hasAngles)
            values.angles = {record.angles[0], record.angles[1], record.angles[2]};
        if ((record.hasPos || record.hasAngles) && NodeEdit::Set(record.name, values))
            ++nodes;
    }
    // (phase 6) the Inspector's field edits (written into the nodes already if the edited copy's level array got them
    // as it loaded - OnNodeArrayAdded; the same values again here), the level's properties.
    std::vector<Fields::NodeEdits> edits;
    for (const SetRecord& record : file.nodes) {
        if (!record.fields.empty())
            edits.push_back({record.name, record.fields});
    }
    int fields = edits.empty() ? 0 : Fields::Apply(edits);
    LevelProps::Set(file.level);
    Picking::SetLocks(file.locks);
    // Chains (phase 4): added rail / path points, relinked and dropped level points, terrains - the rail manager built
    // again with them; peds / cars whose path changed made again.
    if (!Chains::Empty(file.chains))
        Chains::SetState(file.chains);
    // Collision (level_editor_walls.h): invisible walls moved / switched off, drawn pieces' collision switched off - after
    // the sectors (a piece shown by them gets its switch back).
    Walls::ApplyStates(file.walls);
    Walls::Tick();
    int triggers = Triggers::Apply(file.triggers, &message);
    if (!message.empty())
        Log("level editor: %s", message.c_str());
    char summary[360];
    std::snprintf(summary, sizeof(summary),
                  "%u pieces, %u copies, %u rail points, %d objects, %d object copies, %d nodes, %d added, %d hidden, "
                  "%u chain points, %u relinked, %u dropped, %u collision switches / walls, %d triggers, %d fields%s",
                  static_cast<unsigned>(file.sectors.size()), static_cast<unsigned>(clones.size()),
                  static_cast<unsigned>(file.rails.size()), objects, copies, nodes, added, hidden,
                  static_cast<unsigned>(file.chains.points.size()), static_cast<unsigned>(file.chains.links.size()),
                  static_cast<unsigned>(file.chains.dropped.size()), static_cast<unsigned>(file.walls.size()), triggers,
                  fields, LevelProps::Empty(file.level) ? "" : ", level properties");
    return summary;
}

// Everything the editor has changed in this level put back as the level has it, before another layout goes in place
// of it: moved / hidden sectors and rails back, copies of sectors hidden (a layout leaves hidden copies out), moved
// objects back where they first were, copies of objects removed - but those named in `keep` (put in place again by
// Apply).
void ResetToLevel(const File& keep)
{
    // (the chains first: added points removed, links and terrains as the level has them, dropped points shown, the
    // rail manager built again - then the moved rail points below are put back in it)
    Chains::SetState(Chains::State{});
    std::vector<Scenery::SectorState> sectors = Scenery::ChangedSectors();
    for (Scenery::SectorState& state : sectors) {
        state.placement = {0, {0.0f, 0.0f, 0.0f}};
        state.hidden = false;
    }
    for (const Scenery::Clone& clone : Scenery::Clones()) {
        Placement now = {0, {0.0f, 0.0f, 0.0f}};
        Scenery::PlacementNow(clone.checksum, &now);
        if (!Scenery::Hidden(clone.checksum))
            sectors.push_back({clone.checksum, now, true});
    }
    std::vector<Scenery::RailState> rails = Scenery::ChangedRails();
    for (Scenery::RailState& state : rails) {
        state.placement = {0, {0.0f, 0.0f, 0.0f}};
        state.hidden = false;
    }
    Scenery::ApplyStates(sectors, rails);
    Walls::ResetAll(); // (walls back where the level has them, every collision switch as the level had it)
    for (uint32_t id : Objects::Touched()) {
        if (const Objects::State* origin = Objects::Origin(id))
            Objects::Restore(*origin);
    }
    std::vector<Objects::Copy> copies = Objects::Copies();
    for (const Objects::Copy& copy : copies) {
        bool kept = false;
        for (const CopyRecord& record : keep.copies)
            kept = kept || record.name == copy.name;
        if (!kept)
            Objects::RemoveCopy(copy.name);
    }
    NodeEdit::ResetAll();
    Triggers::ResetAll();   // (the editor's trigger volumes and their nodes removed, the level's turned on again)
    NodeCreate::ResetAll(); // (added nodes removed, hidden ones shown: Apply makes the layout's again)
    Fields::ResetAll();
    LevelProps::Set(LevelProps::Props{});
}

// The crash hook's write: no C++ objects here (structured exception handling), no allocation.
void WriteSnapshotGuarded()
{
    __try {
        if (WriteWhole(g_snapshotFolder, g_snapshotPath, g_snapshot.data(), g_snapshot.size()))
            g_snapshotWritten = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

} // namespace

bool Save(std::string* message)
{
    File file;
    if (!Capture(&file)) {
        *message = "Not in a level - nothing saved";
        return false;
    }
    CreateDirectoryA(Folder().c_str(), nullptr);
    std::string path = PathFor(file.scene);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << Write(file);
    out.close();
    if (!out) {
        *message = "Couldn't write " + path;
        Log("level editor: %s", message->c_str());
        return false;
    }
    char text[256];
    std::snprintf(text, sizeof(text),
                  "Saved: %u pieces, %u copies, %u rail points, %u objects, %u object copies, %u nodes, %u added, %u hidden, "
                  "%u triggers",
                  static_cast<unsigned>(file.sectors.size()), static_cast<unsigned>(file.clones.size()),
                  static_cast<unsigned>(file.rails.size()), static_cast<unsigned>(file.objects.size()),
                  static_cast<unsigned>(file.copies.size()), static_cast<unsigned>(file.nodes.size()),
                  static_cast<unsigned>(file.added.size()), static_cast<unsigned>(file.hidden.size()),
                  static_cast<unsigned>(file.triggers.size()));
    *message = text;
    Log("level editor: %s - %s", text, path.c_str());
    // (saved: the autosave's changes are in the layout now, or the user chose these over them)
    std::string autosave = AutosavePathFor(file.scene);
    if (DeleteFileA(autosave.c_str()))
        Log("level editor: autosave deleted - %s", autosave.c_str());
    g_snapshotReady = false;
    g_snapshotWritten = true;
    g_offerPending = false;
    return true;
}

bool Load(std::string* message)
{
    uint8_t* scene = Scenery::MainScene();
    if (Nx::CurrentLevel() == 0 || !scene) {
        *message = "Not in a level";
        return false;
    }
    if (NetworkActive()) {
        *message = "Layouts are offline only - not put in place in a network session";
        Log("level editor: %s", message->c_str());
        return false;
    }
    std::string path = PathFor(Scenery::SceneName(scene));
    File file;
    if (!ReadFor(path, Scenery::SceneName(scene), &file, message, "No saved layout for this level"))
        return false;
    *message = "Layout loaded: " + Apply(file);
    Log("level editor: %s - %s", message->c_str(), path.c_str());
    return true;
}

bool Snapshot()
{
    File file;
    if (!Capture(&file))
        return false;
    std::string text = Write(file);
    std::string path = AutosavePathFor(file.scene);
    if (g_snapshotReady && text == g_snapshot && path == g_snapshotPath)
        return true;
    g_snapshotReady = false; // (the crash hook leaves it alone while it changes)
    g_snapshot.swap(text);
    strncpy_s(g_snapshotPath, path.c_str(), _TRUNCATE);
    strncpy_s(g_snapshotFolder, Folder().c_str(), _TRUNCATE);
    g_snapshotWritten = false;
    g_snapshotReady = true;
    return true;
}

bool WriteAutosave()
{
    if (!g_snapshotReady || g_snapshotWritten || g_offerPending)
        return false;
    if (!WriteWhole(g_snapshotFolder, g_snapshotPath, g_snapshot.data(), g_snapshot.size())) {
        Log("level editor: couldn't write the autosave %s", g_snapshotPath);
        return false;
    }
    g_snapshotWritten = true;
    g_autosaveOurs = true;
    Log("level editor: autosaved - %s", g_snapshotPath);
    return true;
}

void WriteAutosaveFromCrash()
{
    static volatile LONG busy = 0;
    if (!g_snapshotReady || g_snapshotWritten || g_offerPending || InterlockedExchange(&busy, 1))
        return;
    WriteSnapshotGuarded();
    Log(g_snapshotWritten ? "level editor: the unsaved changes were autosaved - %s (Ctrl+R restores them)"
                          : "level editor: the unsaved changes couldn't be autosaved - %s",
        g_snapshotPath);
    busy = 0;
}

bool AutosaveOffered()
{
    uint8_t* scene = Scenery::MainScene();
    if (Nx::CurrentLevel() == 0 || !scene || g_autosaveOurs || NetworkActive())
        return false;
    uint32_t name = Scenery::SceneName(scene);
    uint64_t autosaveTime = 0, savedTime = 0;
    bool haveAutosave = WriteTime(AutosavePathFor(name), &autosaveTime);
    bool haveSaved = WriteTime(PathFor(name), &savedTime);
    g_offerPending = AutosaveNewer(haveAutosave, autosaveTime, haveSaved, savedTime);
    if (g_offerPending)
        Log("level editor: an autosave newer than the saved layout - %s (Ctrl+R restores, Ctrl+Shift+R discards)",
            AutosavePathFor(name).c_str());
    return g_offerPending;
}

bool RestoreAutosave(std::string* message)
{
    uint8_t* scene = Scenery::MainScene();
    if (Nx::CurrentLevel() == 0 || !scene) {
        *message = "Not in a level";
        return false;
    }
    uint32_t name = Scenery::SceneName(scene);
    std::string path = AutosavePathFor(name);
    File file;
    if (!ReadFor(path, name, &file, message, "No autosave for this level"))
        return false;
    ResetToLevel(file);
    *message = "Autosave restored: " + Apply(file);
    Log("level editor: %s - %s", message->c_str(), path.c_str());
    g_offerPending = false;
    g_autosaveOurs = true; // (it's this run's now: the next autosave goes over it)
    return true;
}

bool DiscardAutosave(std::string* message)
{
    uint8_t* scene = Scenery::MainScene();
    if (Nx::CurrentLevel() == 0 || !scene) {
        *message = "Not in a level";
        return false;
    }
    std::string path = AutosavePathFor(Scenery::SceneName(scene));
    g_offerPending = false;
    if (!DeleteFileA(path.c_str())) {
        *message = "No autosave to discard";
        return false;
    }
    g_autosaveOurs = false;
    *message = "Autosave discarded";
    Log("level editor: autosave discarded - %s", path.c_str());
    return true;
}

int LevelLoads() { return g_levelLoads; }

namespace {

// A level of its own from now on (an in-place switch between shipped and edited): the editor's unsaved changes and
// the autosave state start again, as for a new level.
void ForgetSession()
{
    ++g_levelLoads;
    g_snapshotReady = false;
    g_snapshotWritten = true;
    g_autosaveOurs = false;
    g_offerPending = false;
}

// The level picked is the one loaded (the level select doesn't load it again): switched to its edited copy (the layout
// put in place) or back to the level as shipped (everything put back as the level has it).
void SwitchInPlace(int to)
{
    if (NetworkActive())
        return;
    std::string message;
    if (to > 0 && !g_editedCopy) {
        g_editedCopy = true;
        Career::EditedCopy(true); // (the career's gap list kept as it is first - level_editor_career.h)
        Load(&message);
        Log("level editor: switched to the edited copy in place (%s)", message.c_str());
    } else if (to < 0 && g_editedCopy) {
        ResetToLevel(File{});
        g_editedCopy = false;
        Career::EditedCopy(false); // (the career's gap list put back as before)
        Log("level editor: switched back to the level as shipped in place");
    } else {
        return;
    }
    ForgetSession();
}

// The level structures (levels.qb's Level_<x> - load_script=Load_<x>, level="<scene>", name="<title>" - and the
// ported levels', which make_menu_level adds the same way): by load script, the scene and the title.
struct LevelInfo {
    uint32_t scene;
    std::string title;
    Script::CStruct* structure = nullptr; // (phase 6: its level properties go in here - level_editor_level_props.h)
    uint32_t nodeArray = 0;               // (its qb='s node array: SkateScript::NodeArrayChecksum)
};
std::unordered_map<uint32_t, LevelInfo> LevelsByLoad()
{
    static const uint32_t kLoadScript = Crc::GenerateCRCFromString("load_script"),
                          kLevel = Crc::GenerateCRCFromString("level"), kName = Crc::GenerateCRCFromString("name"),
                          kQb = Crc::GenerateCRCFromString("qb");
    std::unordered_map<uint32_t, LevelInfo> levels;
    for (Script::CSymbolTableEntry* e = Script::GetNextSymbol(nullptr); e; e = Script::GetNextSymbol(e)) {
        if (e->type != Script::ESYMBOLTYPE_STRUCTURE || !e->structure)
            continue;
        uint32_t load = 0;
        const char* level = nullptr;
        if (!e->structure->GetChecksum(kLoadScript, &load, 0) || !e->structure->GetString(kLevel, &level, 0))
            continue;
        const char* title = nullptr;
        e->structure->GetString(kName, &title, 0);
        const char* qb = nullptr;
        e->structure->GetString(kQb, &qb, 0);
        levels.emplace(load, LevelInfo{SceneOfLevelText(level), title ? title : "", e->structure,
                                       qb && *qb ? SkateScript::NodeArrayChecksum(qb) : 0});
    }
    return levels;
}

// The scenes with a saved layout (the folder's "<8 hex digits>.txt").
std::vector<uint32_t> SavedScenes()
{
    std::vector<uint32_t> scenes;
    WIN32_FIND_DATAA found;
    HANDLE find = FindFirstFileA((Folder() + "\\*.txt").c_str(), &found);
    if (find == INVALID_HANDLE_VALUE)
        return scenes;
    do {
        uint32_t scene = 0;
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && SavedLayoutScene(found.cFileName, &scene))
            scenes.push_back(scene);
    } while (FindNextFileA(find, &found));
    FindClose(find);
    return scenes;
}

} // namespace

void OnSceneLoaded(const char* name, bool isNet)
{
    (void)name;
    // (a new level: the copies and moved objects of the last are gone with it - and its autosave was written when the
    // editor was left)
    Scenery::Clones().clear();
    Objects::ForgetLevel();
    NodeEdit::ForgetLevel();
    Fields::ForgetLevel();
    LevelProps::ForgetLevel();
    Chains::ForgetLevel(); // (before the overlay goes: the level's links put back while its array is the one loaded)
    NodeCreate::ForgetLevel();
    Picking::ClearLocks();
    g_pending = true; // (Tick decides: the edited copy if it was picked, never in a network session)
    g_wait = 0;
    ++g_levelLoads;
    g_snapshotReady = false;
    g_snapshotWritten = true;
    g_autosaveOurs = false;
    g_offerPending = false;
    g_editedCopy = false;
    Career::EditedCopy(false); // (the career's gap list put back as before the edited copy)
    g_netScene = isNet;
    g_switch = 0;
}

void Tick()
{
    bool up = Nx::CurrentLevel() != 0 && Obj::FindObject(kSkater) && Scenery::MainScene();
    if (g_switch && up && ++g_switchWait >= kSettleFrames) {
        int to = g_switch;
        g_switch = 0;
        SwitchInPlace(to);
    }
    if (!g_pending || !up)
        return;
    if (++g_wait < kSettleFrames)
        return;
    g_pending = false;
    // The level as shipped, unless it was picked as its edited copy (and never in a network session).
    bool network = NetworkActive();
    uint32_t load = CurrentLoad();
    bool apply = ApplyEditedCopy(g_request, load, network);
    g_request = RequestAfterLoad(g_request, load, network);
    // (the level's structure as the level has it again: load_level has read the edited copy's properties by now)
    LevelProps::RestoreStructure();
    if (!apply) {
        if (HasSavedLayout())
            Log("level editor: %s loaded as shipped%s - its edited copy is the level select's (EDITED) entry",
                LevelTitle().c_str(),
                network ? " (a network session: layouts are offline only)" : "");
        return;
    }
    g_editedCopy = true;
    Career::EditedCopy(true); // (the career's gap list kept as it is first - level_editor_career.h)
    std::string message;
    Load(&message);
    LevelProps::OnEditedCopyUp(LevelProps::Current()); // (its start, if it names one)
}

void OnNodeArrayAdded(uint32_t array)
{
    if (!g_request || NetworkActive())
        return;
    // (the level picked as its edited copy - its level structure names its node array (qb=) and its scene (level=))
    std::unordered_map<uint32_t, LevelInfo> levels = LevelsByLoad();
    auto level = levels.find(g_request);
    if (level == levels.end() || !level->second.nodeArray || level->second.nodeArray != array)
        return;
    File file;
    std::string message;
    if (!ReadFor(PathFor(level->second.scene), level->second.scene, &file, &message, "No saved layout"))
        return;
    std::vector<Fields::NodeEdits> edits;
    for (const SetRecord& record : file.nodes) {
        if (!record.fields.empty())
            edits.push_back({record.name, record.fields});
    }
    if (edits.empty())
        return;
    int applied = Fields::ApplyEarly(array, edits);
    Log("level editor: the edited copy's node array %s loaded - %d field edits written into its nodes before they're "
        "made",
        Objects::Label(array).c_str(), applied);
}

bool SavedProps(uint32_t scene, LevelProps::Props* props)
{
    File file;
    std::string message;
    std::ifstream in(PathFor(scene), std::ios::binary);
    if (!in)
        return false;
    std::stringstream text;
    text << in.rdbuf();
    int line = 0;
    if (!Read(text.str(), &file, &line) || file.scene != scene)
        return false;
    *props = file.level;
    return !LevelProps::Empty(file.level);
}

bool NetworkActive()
{
    Net::Manager* net = Net::Manager::Instance();
    return NetworkSession(net && net->IsOnline(), InNetGame(), g_netScene);
}

bool EditingCopy() { return g_editedCopy; }

bool HasSavedLayout()
{
    uint8_t* scene = Scenery::MainScene();
    uint64_t time = 0;
    return Nx::CurrentLevel() != 0 && scene && WriteTime(PathFor(Scenery::SceneName(scene)), &time);
}

std::string LevelTitle()
{
    // (phase 6: the edited copy's own name, as its level properties give it)
    if (g_editedCopy && !LevelProps::Current().name.empty())
        return LevelProps::Current().name;
    // (the symbols walked once a level: the toolbar asks every frame)
    static uint32_t cachedLoad = 0, cachedScene = 0;
    static std::string cached;
    uint32_t load = CurrentLoad(), scene = Scenery::SceneName(Scenery::MainScene());
    if (load != cachedLoad || scene != cachedScene || cached.empty()) {
        cachedLoad = load;
        cachedScene = scene;
        std::unordered_map<uint32_t, LevelInfo> levels = LevelsByLoad();
        auto it = levels.find(load);
        cached = it != levels.end() && !it->second.title.empty() ? it->second.title
                 : scene                                         ? Objects::Label(scene)
                                                                 : "";
    }
    return cached;
}

bool EnterEditedCopy(std::string* message)
{
    if (Nx::CurrentLevel() == 0 || !Scenery::MainScene()) {
        *message = "Not in a level";
        return false;
    }
    if (NetworkActive()) {
        *message = "The level editor is offline only - not in a network session";
        return false;
    }
    g_switch = 0;
    g_request = CurrentLoad(); // (loaded again, it stays the edited copy)
    if (g_editedCopy) {
        message->clear();
        return true;
    }
    g_editedCopy = true;
    Career::EditedCopy(true); // (the career's gap list kept as it is first - level_editor_career.h)
    g_pending = false; // (put in place here, not again by Tick)
    std::string loaded;
    if (HasSavedLayout() && Load(&loaded))
        *message = "Editing the edited copy of " + LevelTitle() + " - its saved layout put in place";
    else
        *message = "A new edited copy of " + LevelTitle() + " - made by the first save (Ctrl+S)";
    Log("level editor: %s (%s)", message->c_str(), loaded.c_str());
    return true;
}

bool ResetLevel(std::string* before, std::string* message)
{
    File file;
    if (!Capture(&file)) {
        *message = "Not in a level";
        return false;
    }
    *before = Write(file);
    ResetToLevel(File{});
    // (the edited copy's files: the saved layout - its level select entry - and the autosave)
    int deleted = 0;
    for (const std::string& path : {PathFor(file.scene), AutosavePathFor(file.scene)}) {
        if (DeleteFileA(path.c_str())) {
            ++deleted;
            Log("level editor: reset - deleted %s", path.c_str());
        }
    }
    g_snapshotReady = false;
    g_snapshotWritten = true;
    g_autosaveOurs = false;
    g_offerPending = false;
    *message = deleted ? "Level reset as shipped, its saved layout deleted (Ctrl+Z undoes)"
                       : "Level reset as shipped (Ctrl+Z undoes)";
    Log("level editor: %s", message->c_str());
    return true;
}

bool CaptureText(std::string* text)
{
    File file;
    if (!Capture(&file))
        return false;
    *text = Write(file);
    return true;
}

bool PutBack(const std::string& before, std::string* message)
{
    File file;
    int line = 0;
    uint8_t* scene = Scenery::MainScene();
    if (Nx::CurrentLevel() == 0 || !scene || !Read(before, &file, &line) || file.scene != Scenery::SceneName(scene)) {
        *message = "Couldn't put the level's changes back";
        return false;
    }
    ResetToLevel(file);
    std::string summary = Apply(file);
    *message = "Reset undone: the level's changes are back (not saved)";
    Log("level editor: %s - %s", message->c_str(), summary.c_str());
    return true;
}

bool __cdecl CFunc_PickLevel(Script::CStruct* params, Script::CScript*)
{
    static const uint32_t kLevel = Crc::GenerateCRCFromString("level"),
                          kEditedLayout = Crc::GenerateCRCFromString("edited_layout");
    uint32_t load = 0;
    if (params)
        params->GetChecksum(kLevel, &load, 0);
    bool edited = params && params->ContainsFlag(kEditedLayout);
    bool network = NetworkActive();
    if (edited && network)
        Log("level editor: an edited copy picked in a network session - refused (the level as shipped)");
    g_request = RequestFromPick(load, edited, network);
    // (phase 6) the edited copy's field edits go into its node array as it loads; its level properties into its level
    // structure now, before load_level reads it (put back once the level is up - Tick - or another level is picked)
    SkateScript::SetNodeArrayAddedHook(&OnNodeArrayAdded);
    LevelProps::RestoreStructure();
    bool loadedAlready = load && load == CurrentLoad() && Nx::CurrentLevel() != 0;
    if (g_request && !loadedAlready) {
        std::unordered_map<uint32_t, LevelInfo> levels = LevelsByLoad();
        auto level = levels.find(g_request);
        LevelProps::Props props;
        if (level != levels.end() && SavedProps(level->second.scene, &props))
            LevelProps::WriteIntoStructure(level->second.structure, props);
    }
    // (the level already loaded: the level select won't load it again - switched in place once its menus are gone)
    if (loadedAlready && !network && (g_request != 0) != g_editedCopy) {
        g_switch = g_request ? 1 : -1;
        g_switchWait = 0;
    }
    Log("level editor: level picked - %s%s", Objects::Label(load).c_str(),
        g_request ? " (its edited copy)" : " (as shipped)");
    return true;
}

bool __cdecl CFunc_EditedLevels(Script::CStruct*, Script::CScript* script)
{
    if (!script || !script->m_params || NetworkActive())
        return false;
    std::vector<uint32_t> saved = SavedScenes();
    if (saved.empty())
        return false;
    static const uint32_t kLevel = Crc::GenerateCRCFromString("level"), kText = Crc::GenerateCRCFromString("text"),
                          kEditedLayout = Crc::GenerateCRCFromString("edited_layout");
    static const char* const kLists[] = {"level_select_menu_level_info", "level_select_menu_thug1_level_info",
                                         "level_select_menu_thps4_level_info", "level_select_menu_thps3_level_info",
                                         "level_select_menu_thaw_level_info"};
    static const char* const kDropped[] = {"new_vmenu", "hold_vmenu"}; // (columns of the level's own section)
    std::unordered_map<uint32_t, LevelInfo> levels = LevelsByLoad();
    std::vector<Script::CStruct*> entries;
    std::vector<uint32_t> done;
    for (const char* list : kLists) {
        Script::CArray* array = Script::GetArray(list);
        if (!array || array->m_type != Script::ESYMBOLTYPE_STRUCTURE)
            continue;
        for (uint32_t i = 0; i < array->m_size; ++i) {
            auto* entry = reinterpret_cast<Script::CStruct*>(array->GetElement(i));
            uint32_t load = 0;
            if (!entry || !entry->GetChecksum(kLevel, &load, 0))
                continue;
            auto level = levels.find(load);
            if (level == levels.end() || std::find(saved.begin(), saved.end(), level->second.scene) == saved.end() ||
                std::find(done.begin(), done.end(), level->second.scene) != done.end())
                continue;
            done.push_back(level->second.scene);
            const char* text = nullptr;
            entry->GetString(kText, &text, 0);
            // (phase 6: the edited copy's own name, when its level properties give it one)
            LevelProps::Props props;
            std::string named;
            if (SavedProps(level->second.scene, &props) && !props.name.empty()) {
                named = props.name;
                text = named.c_str();
            }
            Script::CStruct* copy = Script::NewStruct()->Init();
            copy->AppendStructure(entry);
            for (const char* flag : kDropped)
                copy->RemoveFlag(Crc::GenerateCRCFromString(flag));
            copy->AddString(kText, EditedText(text).c_str());
            copy->AddChecksum(0u, kEditedLayout);
            entries.push_back(copy);
        }
    }
    if (entries.empty())
        return false;
    Script::CArray* array = Script::NewArray()->Init();
    array->SetSizeAndType(static_cast<uint32_t>(entries.size()), Script::ESYMBOLTYPE_STRUCTURE);
    for (size_t i = 0; i < entries.size(); ++i)
        array->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(entries[i]));
    script->m_params->AddArrayPointer("edited_list", array);
    script->m_params->AddInteger("edited_count", static_cast<int>(entries.size()));
    return true;
}

} // namespace Front::LevelEditor::Layout
