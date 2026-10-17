// NOT ORIGINAL - the level editor's saved layouts (ours). See level_editor_layout.h.

#include "game/front/level_editor_layout.h"

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>

#include "game/front/level_editor_objects.h"
#include "game/gfx/screen.h"
#include "game/obj/object.h"
#include "hook/game.h"
#include "hook/log.h"

namespace Front::LevelEditor::Layout {

// --- the text (pure) --------------------------------------------------------------------------------------------

namespace {

const char kHeader[] = "thug2-decomp layout 1";

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

} // namespace

std::string Write(const File& file)
{
    std::ostringstream out;
    out << kHeader << '\n';
    out << "scene";
    Hex(out, file.scene);
    Comment(out, file.scene);
    for (const auto& s : file.sectors) {
        out << "sector";
        Hex(out, s.checksum);
        Placed(out, s.placement);
        out << ' ' << (s.hidden ? 1 : 0);
        Comment(out, s.checksum);
    }
    for (const auto& r : file.rails) {
        out << "rail";
        Hex(out, r.name);
        Placed(out, r.placement);
        out << ' ' << (r.hidden ? 1 : 0);
        Comment(out, r.name);
    }
    for (const auto& c : file.clones) {
        out << "clone";
        Hex(out, c.source);
        Placed(out, c.placement);
        Comment(out, c.source);
    }
    for (const auto& o : file.objects) {
        out << "object";
        Hex(out, o.id);
        for (float v : o.pos)
            Number(out, v);
        for (float v : o.rows)
            Number(out, v);
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
        Comment(out, c.source);
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
            if (line.rfind(kHeader, 0) != 0)
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
            ok = ReadHex(in, &checksum) && ReadPlaced(in, &p) && (in >> hidden);
            if (ok && kind == "sector")
                file->sectors.push_back({checksum, p, hidden != 0});
            else if (ok)
                file->rails.push_back({checksum, p, hidden != 0});
        } else if (kind == "clone") {
            CloneRecord c = {};
            ok = ReadHex(in, &c.source) && ReadPlaced(in, &c.placement);
            if (ok)
                file->clones.push_back(c);
        } else if (kind == "object") {
            ObjectRecord o = {};
            ok = ReadHex(in, &o.id);
            for (float& v : o.pos)
                ok = ok && (in >> v);
            for (float& v : o.rows)
                ok = ok && (in >> v);
            if (ok)
                file->objects.push_back(o);
        } else if (kind == "copy") {
            CopyRecord c = {};
            ok = ReadHex(in, &c.name) && ReadHex(in, &c.source);
            for (float& v : c.pos)
                ok = ok && (in >> v);
            for (float& v : c.rows)
                ok = ok && (in >> v);
            if (ok)
                file->copies.push_back(c);
        } // (anything else: a later version's record - skipped)
        if (!ok)
            return false;
    }
    *lineNumber = 0;
    return header;
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kSkater = 0x5B8AB877; // Skater (the local skater's alias)
constexpr int kSettleFrames = 60;        // (a level's objects and rails are made after its scenes: wait a second)

bool g_pending;
int g_wait;

// The network game (GameNet::Manager 0x007CCE14): layouts aren't put in levels other players share.
bool InNetGame()
{
    void* net = GameVariable<void*>(0x007CCE14);
    return net && GameFunction<bool(__thiscall*)(void*)>(0x0044E4F0)(net);
}

std::string Folder() { return GameFolderPath("thug2-decomp-layouts"); }

std::string PathFor(uint32_t scene)
{
    char name[32];
    std::snprintf(name, sizeof(name), "\\%08X.txt", scene);
    return Folder() + name;
}

void RowsOf(const Mth::Matrix& m, float rows[9])
{
    for (int r = 0; r < 3; ++r) {
        rows[r * 3 + 0] = m.row[r].x;
        rows[r * 3 + 1] = m.row[r].y;
        rows[r * 3 + 2] = m.row[r].z;
    }
}

Objects::State StateOf(uint32_t id, const float pos[3], const float rows[9])
{
    Objects::State state = {};
    state.id = id;
    state.pos = {pos[0], pos[1], pos[2], 1.0f};
    for (int r = 0; r < 3; ++r) {
        state.matrix.row[r] = {rows[r * 3 + 0], rows[r * 3 + 1], rows[r * 3 + 2], 0.0f};
        state.displayMatrix.row[r] = state.matrix.row[r];
    }
    state.matrix.row[3] = state.displayMatrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    return state;
}

} // namespace

bool Save(std::string* message)
{
    uint8_t* scene = Scenery::MainScene();
    if (Nx::CurrentLevel() == 0 || !scene) {
        *message = "Not in a level - nothing saved";
        return false;
    }
    File file;
    file.scene = Scenery::SceneName(scene);
    file.sectors = Scenery::ChangedSectors();
    file.rails = Scenery::ChangedRails();
    for (const Scenery::Clone& clone : Scenery::Clones()) {
        Placement now;
        if (Scenery::PlacementNow(clone.checksum, &now) && !Scenery::Hidden(clone.checksum))
            file.clones.push_back({clone.source, Compose(now, clone.sourceAtCopy)});
    }
    for (uint32_t id : Objects::Touched()) {
        auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(id));
        if (!object || (object->m_objectFlags & Obj::OBJECT_FLAG_DEAD))
            continue;
        ObjectRecord record = {id, {object->m_pos.x, object->m_pos.y, object->m_pos.z}, {}};
        RowsOf(object->m_matrix, record.rows);
        file.objects.push_back(record);
    }
    for (const Objects::Copy& copy : Objects::Copies()) {
        auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(copy.name));
        if (!object || (object->m_objectFlags & Obj::OBJECT_FLAG_DEAD))
            continue;
        CopyRecord record = {copy.name, copy.source, {object->m_pos.x, object->m_pos.y, object->m_pos.z}, {}};
        RowsOf(object->m_matrix, record.rows);
        file.copies.push_back(record);
    }
    CreateDirectoryA(Folder().c_str(), nullptr);
    std::string path = PathFor(file.scene);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << Write(file);
    if (!out) {
        *message = "Couldn't write " + path;
        Log("level editor: %s", message->c_str());
        return false;
    }
    char text[160];
    std::snprintf(text, sizeof(text), "Saved: %u pieces, %u copies, %u rail points, %u objects, %u object copies",
                  static_cast<unsigned>(file.sectors.size()), static_cast<unsigned>(file.clones.size()),
                  static_cast<unsigned>(file.rails.size()), static_cast<unsigned>(file.objects.size()),
                  static_cast<unsigned>(file.copies.size()));
    *message = text;
    Log("level editor: %s - %s", text, path.c_str());
    return true;
}

bool Load(std::string* message)
{
    uint8_t* scene = Scenery::MainScene();
    if (Nx::CurrentLevel() == 0 || !scene) {
        *message = "Not in a level";
        return false;
    }
    std::string path = PathFor(Scenery::SceneName(scene));
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        *message = "No saved layout for this level";
        return false;
    }
    std::stringstream text;
    text << in.rdbuf();
    File file;
    int line = 0;
    if (!Read(text.str(), &file, &line) || file.scene != Scenery::SceneName(scene)) {
        *message = "Couldn't read the saved layout";
        Log("level editor: %s %s (line %d)", message->c_str(), path.c_str(), line);
        return false;
    }
    // Copies first - each made of its source as loaded (the sources aren't moved yet), then put where they were.
    std::vector<Scenery::SectorState> clones;
    for (const CloneRecord& record : file.clones) {
        if (uint32_t made = Scenery::Duplicate(record.source))
            clones.push_back({made, record.placement, false});
    }
    Scenery::ApplyStates(clones, {});
    Scenery::ApplyStates(file.sectors, file.rails);
    int objects = 0, copies = 0;
    for (const ObjectRecord& record : file.objects) {
        if (Objects::Restore(StateOf(record.id, record.pos, record.rows))) {
            Objects::Touch(record.id);
            ++objects;
        }
    }
    for (const CopyRecord& record : file.copies) {
        if (Objects::MakeCopy(record.source, {record.pos[0], record.pos[1], record.pos[2]}, record.name) &&
            Objects::Restore(StateOf(record.name, record.pos, record.rows)))
            ++copies;
    }
    char summary[160];
    std::snprintf(summary, sizeof(summary), "Layout loaded: %u pieces, %u copies, %u rail points, %d objects, %d object copies",
                  static_cast<unsigned>(file.sectors.size()), static_cast<unsigned>(clones.size()),
                  static_cast<unsigned>(file.rails.size()), objects, copies);
    *message = summary;
    Log("level editor: %s - %s", summary, path.c_str());
    return true;
}

void OnSceneLoaded(const char* name, bool isNet)
{
    (void)name;
    // (a new level: the copies and moved objects of the last are gone with it)
    Scenery::Clones().clear();
    Objects::ForgetLevel();
    g_pending = !isNet;
    g_wait = 0;
}

void Tick()
{
    if (!g_pending || Nx::CurrentLevel() == 0 || !Obj::FindObject(kSkater) || !Scenery::MainScene())
        return;
    if (++g_wait < kSettleFrames)
        return;
    g_pending = false;
    if (InNetGame())
        return;
    std::string message;
    Load(&message);
}

} // namespace Front::LevelEditor::Layout
