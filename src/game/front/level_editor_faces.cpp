// NOT ORIGINAL - the level editor's collision face flags (ours). See level_editor_faces.h.

#include "game/front/level_editor_faces.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>

#include "game/front/level_editor_actions.h"
#include "game/front/level_editor_hud.h"
#include "game/front/level_editor_modules.h"
#include "game/front/level_editor_objects.h"
#include "game/front/level_editor_scenery.h"
#include "game/front/level_editor_shared_coll.h"
#include "game/front/level_editor_view.h"
#include "game/nx/coll_tri.h"
#include "hook/log.h"
#include "game/core/game_vtables.h"
#include "imgui.h"

namespace Front::LevelEditor::Faces {

// --- the pure parts -------------------------------------------------------------------------------------------------

namespace {

const FlagInfo kFlags[kFlagCount] = {
    {0x0001, "Skatable", "Skatable at any slope"},
    {0x0002, "Not Skatable", "Not skatable unless Skatable or Vert is set"},
    {0x0004, "Wallride", "Wallride only"},
    {0x0008, "Vert", "Launches like a quarter pipe"},
    {0x0010, "Not Collidable", "Skater, camera and objects pass through"},
    {0x0040, "Trigger", "Runs its node's TriggerScript when crossed"},
    {0x0080, "Camera Pass-Through", "Camera passes through"},
    {0x2000, "Not Taggable", "Can't be tagged with graffiti"},
};

int HexDigit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

} // namespace

const FlagInfo& Flag(int index) { return kFlags[std::clamp(index, 0, kFlagCount - 1)]; }

uint16_t Applied(uint16_t flags, uint16_t set, uint16_t clear)
{
    return static_cast<uint16_t>((flags | set) & ~clear);
}

int Combined(const std::vector<uint16_t>& flags, uint16_t bit)
{
    if (flags.empty())
        return -1;
    size_t on = 0;
    for (uint16_t f : flags)
        on += (f & bit) ? 1 : 0;
    return on == 0 ? 0 : on == flags.size() ? 1 : 2;
}

bool Click(std::vector<FaceKey>* selection, FaceKey key, bool add)
{
    auto it = std::find(selection->begin(), selection->end(), key);
    if (!add) {
        selection->assign(1, key);
        return true;
    }
    if (it != selection->end()) {
        selection->erase(it);
        return false;
    }
    selection->push_back(key);
    return true;
}

std::string WriteRecords(const std::vector<SectorFaces>& sectors)
{
    std::string out;
    char text[32];
    for (const SectorFaces& s : sectors) {
        if (s.faces.empty())
            continue;
        std::snprintf(text, sizeof(text), "faces %08X ", s.checksum);
        out += text;
        for (size_t i = 0; i < s.faces.size(); ++i) {
            std::snprintf(text, sizeof(text), "%s%u:%04X", i ? "," : "", static_cast<unsigned>(s.faces[i].first),
                          static_cast<unsigned>(s.faces[i].second));
            out += text;
        }
        out += '\n';
    }
    return out;
}

bool ReadRecord(std::istringstream& in, SectorFaces* out)
{
    SectorFaces s;
    std::string checksum, list;
    if (!(in >> checksum >> list) || checksum.size() != 8)
        return false;
    uint32_t value = 0;
    for (char c : checksum) {
        int d = HexDigit(c);
        if (d < 0)
            return false;
        value = value << 4 | static_cast<uint32_t>(d);
    }
    s.checksum = value;
    size_t at = 0;
    while (at <= list.size()) {
        size_t comma = list.find(',', at);
        std::string item = list.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        size_t colon = item.find(':');
        if (colon == std::string::npos || colon == 0 || colon + 1 >= item.size() || item.size() - colon - 1 > 4)
            return false;
        uint32_t face = 0;
        for (size_t i = 0; i < colon; ++i) {
            if (item[i] < '0' || item[i] > '9' || face > 6553)
                return false;
            face = face * 10 + static_cast<uint32_t>(item[i] - '0');
        }
        if (face > 0xFFFF)
            return false;
        uint32_t flags = 0;
        for (size_t i = colon + 1; i < item.size(); ++i) {
            int d = HexDigit(item[i]);
            if (d < 0)
                return false;
            flags = flags << 4 | static_cast<uint32_t>(d);
        }
        for (const auto& f : s.faces)
            if (f.first == face)
                return false; // (a face twice: not a record we wrote)
        s.faces.push_back({static_cast<uint16_t>(face), static_cast<uint16_t>(flags)});
        if (comma == std::string::npos)
            break;
        at = comma + 1;
    }
    std::string more;
    if (s.faces.empty() || (in >> more))
        return false;
    std::sort(s.faces.begin(), s.faces.end());
    *out = s;
    return true;
}

std::vector<SectorFaces> Group(const std::vector<Change>& changes)
{
    std::map<uint32_t, std::vector<std::pair<uint16_t, uint16_t>>> bySector;
    for (const Change& c : changes) {
        if (c.now != c.origin)
            bySector[c.key.checksum].push_back({c.key.face, c.now});
    }
    std::vector<SectorFaces> out;
    for (auto& entry : bySector) {
        std::sort(entry.second.begin(), entry.second.end());
        out.push_back({entry.first, entry.second});
    }
    return out;
}

// --- the game side --------------------------------------------------------------------------------------------------

namespace {

void** const& kCollStaticVtable = Vt::kCollStatic;
constexpr size_t kMostSelected = 8192;

bool g_picking;
std::vector<FaceKey> g_selection;
bool g_haveHover;
FaceKey g_hover;
std::map<FaceKey, uint16_t> g_origins; // (each face the editor changed: its flags as the level has them)
std::vector<std::function<void()>> g_queue;

// The sector's collision geometry (its CCollStatic's), null for none.
Nx::CCollObjTriData* Geometry(uint32_t checksum)
{
    uint8_t* sector = Scenery::SectorOf(checksum);
    uint8_t* coll = sector ? *reinterpret_cast<uint8_t**>(sector + 0x18) : nullptr;
    if (!coll || *reinterpret_cast<void***>(coll) != kCollStaticVtable)
        return nullptr;
    auto* tri = *reinterpret_cast<Nx::CCollObjTriData**>(coll + 0x0C);
    return tri && tri->m_faces ? tri : nullptr;
}

uint16_t* Raw(Nx::CCollObjTriData* tri, int face)
{
    return reinterpret_cast<uint16_t*>(tri->m_faces + face * (tri->m_smallFaces ? 8 : 10));
}

// The face's flags written (a taken face: in its companion - the original keeps 0x10); its level's flags noted first.
bool Write(FaceKey key, uint16_t flags)
{
    Nx::CCollObjTriData* tri = Geometry(key.checksum);
    if (!tri || key.face >= tri->m_numFaces)
        return false;
    if (!g_origins.count(key)) {
        uint16_t origin = 0;
        if (!Read(key, &origin))
            return false;
        g_origins[key] = origin;
    }
    if (!SharedCollision::SetTakenFlags(tri, key.face, flags))
        *Raw(tri, key.face) = flags;
    return true;
}

// The face's corners where the skater meets it (a taken face: its companion's).
bool Corners(FaceKey key, V3 out[3])
{
    Nx::CCollObjTriData* tri = Geometry(key.checksum);
    if (!tri || key.face >= tri->m_numFaces || !tri->m_vertices)
        return false;
    uint32_t piece = 0;
    int index = -1;
    if (SharedCollision::TakenFace(tri, key.face, &piece, &index, nullptr)) {
        const SharedCollision::Companion* companion = SharedCollision::CompanionOf(piece);
        if (!companion || index >= companion->FaceCount())
            return false;
        for (int k = 0; k < 3; ++k) {
            uint32_t v = companion->Geometry().FaceVertex(index, k);
            if (v >= static_cast<uint32_t>(companion->VertexCount()))
                return false;
            out[k] = companion->Vertex(static_cast<int>(v));
        }
        return true;
    }
    for (int k = 0; k < 3; ++k) {
        uint32_t v = tri->FaceVertex(key.face, k);
        if (v >= tri->m_numVertices)
            return false;
        Mth::Vector p;
        tri->GetRawVertexPos(static_cast<int>(v), &p);
        out[k] = {p.x, p.y, p.z};
    }
    return true;
}

// A face the ray hit, as a level face (a companion's: the face it was taken from). False: a copy's face (nothing to
// save), or gone.
bool KeyOf(const View::FaceHit& hit, FaceKey* key, std::string* why)
{
    uint32_t checksum = hit.checksum;
    int face = hit.face;
    if (hit.companion) {
        Nx::CCollObjTriData* geometry = nullptr;
        if (!SharedCollision::OriginOf(hit.checksum, hit.face, &geometry, &face) || !geometry) {
            *why = "Face belongs to a copy; edit the source";
            return false;
        }
        checksum = geometry->m_checksum;
    }
    if (Scenery::IsClone(checksum)) {
        *why = "Face belongs to a copy; edit the source";
        return false;
    }
    if (!Geometry(checksum) || face < 0 || face > 0xFFFF) {
        *why = "Not a level collision face";
        return false;
    }
    *key = {checksum, static_cast<uint16_t>(face)};
    return true;
}

struct Step : ModuleStep {
    std::vector<std::pair<FaceKey, uint16_t>> flags;
    std::vector<FaceKey> selection;
    void Restore() override
    {
        for (const auto& f : flags)
            Write(f.first, f.second);
        g_selection = selection;
        View::InvalidateCache();
    }
    std::shared_ptr<ModuleStep> Now() const override
    {
        auto now = std::make_shared<Step>();
        for (const auto& f : flags) {
            uint16_t value = f.second;
            Read(f.first, &value);
            now->flags.push_back({f.first, value});
        }
        now->selection = g_selection;
        return now;
    }
};

std::shared_ptr<Step> Capture(const std::vector<FaceKey>& keys)
{
    auto step = std::make_shared<Step>();
    for (const FaceKey& key : keys) {
        uint16_t value = 0;
        if (Read(key, &value))
            step->flags.push_back({key, value});
    }
    step->selection = g_selection;
    return step;
}

std::string PieceLabel(uint32_t checksum) { return Objects::Label(checksum); }

} // namespace

bool Picking() { return g_picking; }

void SetPicking(bool on)
{
    // The collision view comes on with Face Select and goes off with it again if Face Select turned it on.
    static bool s_turnedCollisionOn;
    if (on && !g_picking) {
        s_turnedCollisionOn = !View::ShowCollision();
        if (s_turnedCollisionOn)
            View::SetShowCollision(true);
    } else if (!on && g_picking) {
        if (s_turnedCollisionOn && View::ShowCollision())
            View::SetShowCollision(false);
        s_turnedCollisionOn = false;
    }
    g_picking = on;
    if (!on)
        g_haveHover = false;
}

void Start()
{
    Actions::SetChecked(Actions::kFaces, [] { return g_picking ? 1 : 0; }); // (its check in the View menu)
    g_picking = false;
    g_selection.clear();
    g_haveHover = false;
}

void Stop()
{
    g_picking = false;
    g_selection.clear();
    g_haveHover = false;
    g_queue.clear();
}

const std::vector<FaceKey>& Selection() { return g_selection; }
bool HasSelection() { return !g_selection.empty(); }
void ClearSelection() { g_selection.clear(); }

bool ClickAt(V3 origin, V3 unitDir, bool add)
{
    View::FaceHit hit;
    if (!View::PickFace(origin, unitDir, &hit)) {
        if (!add)
            g_selection.clear();
        Hud::Message("No collision face under cursor");
        return false;
    }
    FaceKey key;
    std::string why;
    if (!KeyOf(hit, &key, &why)) {
        Hud::Message(why, 4.0);
        return false;
    }
    if (add && g_selection.size() >= kMostSelected && std::find(g_selection.begin(), g_selection.end(), key) == g_selection.end()) {
        Hud::Message("Limit: 8192 faces");
        return false;
    }
    bool selected = Click(&g_selection, key, add);
    uint16_t flags = 0;
    Read(key, &flags);
    char text[160];
    std::snprintf(text, sizeof(text), "Face %u of %s %s - flags 0x%04X (%u selected)", static_cast<unsigned>(key.face),
                  PieceLabel(key.checksum).c_str(), selected ? "selected" : "dropped", flags,
                  static_cast<unsigned>(g_selection.size()));
    Hud::Message(text);
    if (DebugEnabled())
        Log("level editor: %s", text);
    return true;
}

void Hover(V3 origin, V3 unitDir, bool haveRay)
{
    g_haveHover = false;
    View::FaceHit hit;
    std::string why;
    if (g_picking && haveRay && View::PickFace(origin, unitDir, &hit) && KeyOf(hit, &g_hover, &why))
        g_haveHover = true;
}

bool SelectPiece(uint32_t checksum, bool add)
{
    std::vector<FaceKey> faces;
    if (Scenery::IsClone(checksum))
        return false;
    if (Nx::CCollObjTriData* tri = Geometry(checksum)) {
        for (int f = 0; f < tri->m_numFaces && faces.size() < kMostSelected; ++f)
            faces.push_back({checksum, static_cast<uint16_t>(f)});
    }
    // (the faces its shared collision took along: theirs to edit too)
    if (const SharedCollision::Companion* companion = SharedCollision::CompanionOf(checksum)) {
        for (int i = 0; i < companion->FaceCount() && faces.size() < kMostSelected; ++i) {
            Nx::CCollObjTriData* geometry = nullptr;
            int face = -1;
            if (SharedCollision::OriginOf(checksum, i, &geometry, &face) && geometry && face >= 0)
                faces.push_back({geometry->m_checksum, static_cast<uint16_t>(face)});
        }
    }
    if (faces.empty())
        return false;
    if (!add)
        g_selection.clear();
    for (const FaceKey& key : faces) {
        if (g_selection.size() >= kMostSelected)
            break;
        if (std::find(g_selection.begin(), g_selection.end(), key) == g_selection.end())
            g_selection.push_back(key);
    }
    return true;
}

bool Read(FaceKey key, uint16_t* flags)
{
    Nx::CCollObjTriData* tri = Geometry(key.checksum);
    if (!tri || key.face >= tri->m_numFaces)
        return false;
    if (!SharedCollision::TakenFace(tri, key.face, nullptr, nullptr, flags))
        *flags = *Raw(tri, key.face);
    return true;
}

bool SetFlag(uint16_t bit, bool on)
{
    if (g_selection.empty())
        return false;
    std::shared_ptr<Step> before = Capture(g_selection);
    bool changed = false;
    int faces = 0;
    for (const FaceKey& key : g_selection) {
        uint16_t flags = 0;
        if (!Read(key, &flags))
            continue;
        uint16_t now = on ? Applied(flags, bit, 0) : Applied(flags, 0, bit);
        if (now != flags && Write(key, now)) {
            changed = true;
            ++faces;
        }
    }
    if (!changed)
        return false;
    PushModuleUndo(before);
    View::InvalidateCache();
    char text[128];
    std::snprintf(text, sizeof(text), "Face flag 0x%04X %s on %d face%s", bit,
                  on ? "set" : "cleared", faces, faces == 1 ? "" : "s");
    Hud::Message(text);
    Log("level editor: %s", text);
    return true;
}

bool HomeSelection()
{
    if (g_selection.empty())
        return false;
    std::shared_ptr<Step> before = Capture(g_selection);
    int faces = 0;
    for (const FaceKey& key : g_selection) {
        auto origin = g_origins.find(key);
        uint16_t now = 0;
        if (origin != g_origins.end() && Read(key, &now) && now != origin->second && Write(key, origin->second))
            ++faces;
    }
    if (faces) {
        PushModuleUndo(before);
        View::InvalidateCache();
    }
    Hud::Message(faces ? "Face flags reverted" : "Face flags already original");
    return true;
}

std::vector<SectorFaces> Changed()
{
    std::vector<Change> changes;
    for (const auto& origin : g_origins) {
        uint16_t now = 0;
        if (Read(origin.first, &now))
            changes.push_back({origin.first, origin.second, now});
    }
    return Group(changes);
}

int Apply(const std::vector<SectorFaces>& sectors)
{
    int count = 0, missing = 0;
    for (const SectorFaces& s : sectors) {
        for (const auto& face : s.faces) {
            if (Write({s.checksum, face.first}, face.second))
                ++count;
            else
                ++missing;
        }
    }
    if (count)
        View::InvalidateCache();
    if (missing)
        Log("level editor: %d edited collision faces aren't in this level's collision - skipped", missing);
    return count;
}

void ResetAll()
{
    for (const auto& origin : g_origins) {
        uint16_t now = 0;
        if (Read(origin.first, &now) && now != origin.second)
            Write(origin.first, origin.second);
    }
    g_selection.clear();
    View::InvalidateCache();
}

void ForgetLevel()
{
    g_origins.clear();
    g_selection.clear();
    g_haveHover = false;
    g_queue.clear();
}

void Update()
{
    std::vector<std::function<void()>> queue;
    queue.swap(g_queue);
    for (auto& action : queue)
        action();
    // (faces of a level gone: dropped from the selection)
    g_selection.erase(std::remove_if(g_selection.begin(), g_selection.end(),
                                     [](const FaceKey& key) {
                                         uint16_t flags;
                                         return !Read(key, &flags);
                                     }),
                      g_selection.end());
}

void AddOverlay(V3 cameraPos, const Frame& frame, const Lens& lens, const uint32_t viewport[4], float nearZ,
                std::vector<Ui::Line>* lines)
{
    auto outline = [&](FaceKey key, uint32_t argb, float thickness) {
        V3 p[3];
        if (!Corners(key, p))
            return;
        for (int k = 0; k < 3; ++k) {
            V3 a = p[k], b = p[(k + 1) % 3];
            if (!ClipToNear(cameraPos, frame, nearZ + 0.01f, &a, &b))
                continue;
            float ax, ay, bx, by, nx, ny;
            if (!Project(cameraPos, frame, lens, a, nearZ, &nx, &ny))
                continue;
            NdcToPixel(viewport, nx, ny, &ax, &ay);
            if (!Project(cameraPos, frame, lens, b, nearZ, &nx, &ny))
                continue;
            NdcToPixel(viewport, nx, ny, &bx, &by);
            lines->push_back({ax, ay, bx, by, argb, thickness});
        }
    };
    size_t drawn = 0;
    for (const FaceKey& key : g_selection) {
        if (++drawn > 4096)
            break;
        outline(key, 0xFF40F0FF, 2.0f);
    }
    if (g_picking && g_haveHover)
        outline(g_hover, 0xFFFFF040, 2.5f);
}

std::string Describe()
{
    char text[96];
    std::snprintf(text, sizeof(text), "Faces mode%s: %u selected", g_picking ? "" : " off",
                  static_cast<unsigned>(g_selection.size()));
    return text;
}

void InspectorSection(ImFont* mono, ImFont* smallFont)
{
    if (!g_picking && g_selection.empty())
        return;
    ImGui::Spacing();
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Collision faces###faceflags", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::PushFont(smallFont);
    ImGui::TextDisabled("%s", g_picking ? "Click a face to select, Shift+click to add" : "Face Select is off");
    ImGui::PopFont();
    bool picking = g_picking;
    if (ImGui::Checkbox("Face Select Mode", &picking))
        g_queue.push_back([picking] { SetPicking(picking); });
    if (g_selection.empty()) {
        ImGui::TextDisabled("No faces selected");
        return;
    }
    std::vector<uint16_t> flags;
    std::vector<uint32_t> pieces;
    for (const FaceKey& key : g_selection) {
        uint16_t f = 0;
        if (Read(key, &f))
            flags.push_back(f);
        if (std::find(pieces.begin(), pieces.end(), key.checksum) == pieces.end())
            pieces.push_back(key.checksum);
    }
    ImGui::PushFont(mono);
    if (g_selection.size() == 1 && !flags.empty())
        ImGui::Text("face %u of %s: 0x%04X", static_cast<unsigned>(g_selection[0].face),
                    PieceLabel(g_selection[0].checksum).c_str(), flags[0]);
    else
        ImGui::Text("%u faces in %u piece%s", static_cast<unsigned>(g_selection.size()),
                    static_cast<unsigned>(pieces.size()), pieces.size() == 1 ? "" : "s");
    ImGui::PopFont();
    for (int i = 0; i < kFlagCount; ++i) {
        const FlagInfo& info = kFlags[i];
        int state = Combined(flags, info.bit);
        bool on = state == 1;
        char label[64];
        std::snprintf(label, sizeof(label), "%s%s##face%d", info.name, state == 2 ? " (some)" : "", i);
        if (ImGui::Checkbox(label, &on)) {
            uint16_t bit = info.bit;
            bool set = state != 1; // (mixed or clear: set on all; all set: clear)
            g_queue.push_back([bit, set] { SetFlag(bit, set); });
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", info.what);
    }
    if (ImGui::Button("Home (the level's flags)##faces"))
        g_queue.push_back([] { HomeSelection(); });
    ImGui::SameLine();
    if (ImGui::Button("Drop selection##faces"))
        g_queue.push_back([] { ClearSelection(); });
    uint32_t piece = g_selection.back().checksum;
    std::string allLabel = "All faces of " + PieceLabel(piece);
    if (ImGui::Button(allLabel.c_str()))
        g_queue.push_back([piece] {
            if (SelectPiece(piece, false))
                Hud::Message("Selected all faces of " + PieceLabel(piece));
        });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Select all faces of this piece");
    if (Scenery::HasSelection()) {
        ImGui::SameLine();
        if (ImGui::Button("Select All Faces"))
            g_queue.push_back([] {
                std::vector<uint32_t> sectors = Scenery::Selection();
                bool any = false;
                for (size_t i = 0; i < sectors.size(); ++i)
                    any = SelectPiece(sectors[i], any) || any;
                Hud::Message(any ? "Selected all faces" : "Selection has no collision");
            });
    }
}

} // namespace Front::LevelEditor::Faces
