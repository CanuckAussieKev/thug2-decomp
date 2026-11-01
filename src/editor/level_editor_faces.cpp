// NOT ORIGINAL - the level editor's collision face flags (ours). See level_editor_faces.h.

#include "editor/level_editor_faces.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>

#include "editor/level_editor_actions.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_shared_coll.h"
#include "editor/level_editor_view.h"
#include "engine/render/coll_static.h"
#include "engine/render/coll_tri.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "engine/gfx/sector.h"

namespace frontend::level_editor::faces {

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

int hex_digit(char c)
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

const FlagInfo& bit_mark(int index) { return kFlags[std::clamp(index, 0, kFlagCount - 1)]; }

uint16_t get_applied(uint16_t flags, uint16_t set, uint16_t clear)
{
    return static_cast<uint16_t>((flags | set) & ~clear);
}

int get_combined(const std::vector<uint16_t>& flags, uint16_t bit)
{
    if (flags.empty())
        return -1;
    size_t on = 0;
    for (uint16_t f : flags)
        on += (f & bit) ? 1 : 0;
    return on == 0 ? 0 : on == flags.size() ? 1 : 2;
}

bool get_click(std::vector<FaceKey>* selection, FaceKey key, bool add)
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

std::string write_records(const std::vector<SectorFaces>& sectors)
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

bool read_record(std::istringstream& in, SectorFaces* out)
{
    SectorFaces s;
    std::string checksum, list;
    if (!(in >> checksum >> list) || checksum.size() != 8)
        return false;
    uint32_t value = 0;
    for (char c : checksum) {
        int d = hex_digit(c);
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
            int d = hex_digit(item[i]);
            if (d < 0)
                return false;
            flags = flags << 4 | static_cast<uint32_t>(d);
        }
        for (const auto& f : s.faces)
            if (f.first == face)
                return false; // (a face twice: not a record we wrote)
        s.faces.emplace_back(static_cast<uint16_t>(face), static_cast<uint16_t>(flags));
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

std::vector<SectorFaces> group_set(const std::vector<Change>& changes)
{
    std::map<uint32_t, std::vector<std::pair<uint16_t, uint16_t>>> by_sector;
    for (const Change& c : changes) {
        if (c.now != c.origin)
            by_sector[c.key.checksum].emplace_back(c.key.face, c.now);
    }
    std::vector<SectorFaces> out;
    for (auto& entry : by_sector) {
        std::sort(entry.second.begin(), entry.second.end());
        out.push_back({entry.first, entry.second});
    }
    return out;
}

// --- the game side --------------------------------------------------------------------------------------------------

namespace {

constexpr size_t kMostSelected = 8192;

bool g_picking;
std::vector<FaceKey> g_selection;
bool g_have_hover;
FaceKey g_hover;
std::map<FaceKey, uint16_t> g_origins; // (each face the editor changed: its flags as the level has them)
std::vector<std::function<void()>> g_queue;

// The sector's collision geometry (its StaticCollider's), null for none.
render::TriMeshSource* get_geometry(uint32_t checksum)
{
    render::WorldCell* sector = scenery::sector_of(checksum);
    render::HitMesh* coll = sector ? sector->hit_body_ : nullptr;
    if (!render::is_static_collider(coll))
        return nullptr;
    auto* tri = coll->geometry_;
    return tri && tri->faces_ ? tri : nullptr;
}

uint16_t* get_raw(render::TriMeshSource* tri, int face)
{
    return reinterpret_cast<uint16_t*>(tri->faces_ + face * (tri->small_faces_ ? 8 : 10));
}

// The face's flags written (a taken face: in its companion - the original keeps 0x10); its level's flags noted first.
bool emit_out(FaceKey key, uint16_t flags)
{
    render::TriMeshSource* tri = get_geometry(key.checksum);
    if (!tri || key.face >= tri->tri_count_)
        return false;
    if (!g_origins.count(key)) {
        uint16_t origin = 0;
        if (!get_take_in(key, &origin))
            return false;
        g_origins[key] = origin;
    }
    if (!shared_collision::set_taken_flags(tri, key.face, flags))
        *get_raw(tri, key.face) = flags;
    return true;
}

// The face's corners where the skater meets it (a taken face: its companion's).
bool get_corners(FaceKey key, V3 out[3])
{
    render::TriMeshSource* tri = get_geometry(key.checksum);
    if (!tri || key.face >= tri->tri_count_ || !tri->vertices_)
        return false;
    uint32_t piece = 0;
    int index = -1;
    if (shared_collision::taken_face(tri, key.face, &piece, &index, nullptr)) {
        const shared_collision::Companion* companion = shared_collision::companion_of(piece);
        if (!companion || index >= companion->face_count())
            return false;
        for (int k = 0; k < 3; ++k) {
            uint32_t v = companion->get_geometry().face_vertex(index, k);
            if (v >= static_cast<uint32_t>(companion->get_vertex_count()))
                return false;
            out[k] = companion->get_vertex(static_cast<int>(v));
        }
        return true;
    }
    for (int k = 0; k < 3; ++k) {
        uint32_t v = tri->face_vertex(key.face, k);
        if (v >= tri->num_vertices_)
            return false;
        maths::Vec4 p{};
        tri->read_stored_vertex(static_cast<int>(v), &p);
        out[k] = {p.x, p.y, p.z};
    }
    return true;
}

// A face the ray hit, as a level face (a companion's: the face it was taken from). False: a copy's face (nothing to
// save), or gone.
bool get_key_of(const view::FaceHit& hit, FaceKey* key, std::string* why)
{
    uint32_t checksum = hit.checksum;
    int face = hit.face;
    if (hit.companion) {
        render::TriMeshSource* geometry = nullptr;
        if (!shared_collision::origin_of(hit.checksum, hit.face, &geometry, &face) || !geometry) {
            *why = "Face belongs to a copy; edit the source";
            return false;
        }
        checksum = geometry->checksum_;
    }
    if (scenery::is_clone(checksum)) {
        *why = "Face belongs to a copy; edit the source";
        return false;
    }
    if (!get_geometry(checksum) || face < 0 || face > 0xFFFF) {
        *why = "Not a level collision face";
        return false;
    }
    *key = {checksum, static_cast<uint16_t>(face)};
    return true;
}

struct Step : ModuleStep {
    std::vector<std::pair<FaceKey, uint16_t>> flags;
    std::vector<FaceKey> selection;
    void get_restore() override
    {
        for (const auto& f : flags)
            emit_out(f.first, f.second);
        g_selection = selection;
        view::stale_cache();
    }
    std::shared_ptr<ModuleStep> get_now() const override
    {
        auto now = std::make_shared<Step>();
        for (const auto& f : flags) {
            uint16_t value = f.second;
            get_take_in(f.first, &value);
            now->flags.emplace_back(f.first, value);
        }
        now->selection = g_selection;
        return now;
    }
};

std::shared_ptr<Step> get_capture(const std::vector<FaceKey>& keys)
{
    auto step = std::make_shared<Step>();
    for (const FaceKey& key : keys) {
        uint16_t value = 0;
        if (get_take_in(key, &value))
            step->flags.emplace_back(key, value);
    }
    step->selection = g_selection;
    return step;
}

std::string piece_label(uint32_t checksum) { return object_edit::caption(checksum); }

} // namespace

bool get_picking() { return g_picking; }

void set_picking(bool on)
{
    // The collision view comes on with Face Select and goes off with it again if Face Select turned it on.
    static bool s_turned_collision_on;
    if (on && !g_picking) {
        s_turned_collision_on = !view::show_collision();
        if (s_turned_collision_on)
            view::set_show_collision(true);
    } else if (!on && g_picking) {
        if (s_turned_collision_on && view::show_collision())
            view::set_show_collision(false);
        s_turned_collision_on = false;
    }
    g_picking = on;
    if (!on)
        g_have_hover = false;
}

void kickoff()
{
    actions::set_checked(actions::kFaces, [] { return g_picking ? 1 : 0; }); // (its check in the View menu)
    g_picking = false;
    g_selection.clear();
    g_have_hover = false;
}

void halt()
{
    g_picking = false;
    g_selection.clear();
    g_have_hover = false;
    g_queue.clear();
}

const std::vector<FaceKey>& get_selection() { return g_selection; }
bool has_selection() { return !g_selection.empty(); }
void clear_selection() { g_selection.clear(); }

bool click_at(V3 origin, V3 unit_dir, bool add)
{
    view::FaceHit hit;
    if (!view::pick_face(origin, unit_dir, &hit)) {
        if (!add)
            g_selection.clear();
        hud::get_message("No collision face under cursor");
        return false;
    }
    FaceKey key{};
    std::string why;
    if (!get_key_of(hit, &key, &why)) {
        hud::get_message(why, 4.0);
        return false;
    }
    if (add && g_selection.size() >= kMostSelected &&
        std::find(g_selection.begin(), g_selection.end(), key) == g_selection.end()) {
        hud::get_message("Limit: 8192 faces");
        return false;
    }
    bool selected = get_click(&g_selection, key, add);
    uint16_t flags = 0;
    get_take_in(key, &flags);
    char text[160];
    std::snprintf(text, sizeof(text), "Face %u of %s %s - flags 0x%04X (%u selected)", static_cast<unsigned>(key.face),
                  piece_label(key.checksum).c_str(), selected ? "selected" : "dropped", flags,
                  static_cast<unsigned>(g_selection.size()));
    hud::get_message(text);
    if (debug_enabled())
        get_log("level editor: %s", text);
    return true;
}

void get_hover(V3 origin, V3 unit_dir, bool have_ray)
{
    g_have_hover = false;
    view::FaceHit hit;
    std::string why;
    if (g_picking && have_ray && view::pick_face(origin, unit_dir, &hit) && get_key_of(hit, &g_hover, &why))
        g_have_hover = true;
}

bool select_piece(uint32_t checksum, bool add)
{
    std::vector<FaceKey> faces;
    if (scenery::is_clone(checksum))
        return false;
    if (render::TriMeshSource* tri = get_geometry(checksum)) {
        for (int f = 0; f < tri->tri_count_ && faces.size() < kMostSelected; ++f)
            faces.push_back({checksum, static_cast<uint16_t>(f)});
    }
    // (the faces its shared collision took along: theirs to edit too)
    if (const shared_collision::Companion* companion = shared_collision::companion_of(checksum)) {
        for (int i = 0; i < companion->face_count() && faces.size() < kMostSelected; ++i) {
            render::TriMeshSource* geometry = nullptr;
            int face = -1;
            if (shared_collision::origin_of(checksum, i, &geometry, &face) && geometry && face >= 0)
                faces.push_back({geometry->checksum_, static_cast<uint16_t>(face)});
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

bool get_take_in(FaceKey key, uint16_t* flags)
{
    render::TriMeshSource* tri = get_geometry(key.checksum);
    if (!tri || key.face >= tri->tri_count_)
        return false;
    if (!shared_collision::taken_face(tri, key.face, nullptr, nullptr, flags))
        *flags = *get_raw(tri, key.face);
    return true;
}

bool set_flag(uint16_t bit, bool on)
{
    if (g_selection.empty())
        return false;
    std::shared_ptr<Step> before = get_capture(g_selection);
    bool changed = false;
    int faces = 0;
    for (const FaceKey& key : g_selection) {
        uint16_t flags = 0;
        if (!get_take_in(key, &flags))
            continue;
        uint16_t now = on ? get_applied(flags, bit, 0) : get_applied(flags, 0, bit);
        if (now != flags && emit_out(key, now)) {
            changed = true;
            ++faces;
        }
    }
    if (!changed)
        return false;
    push_module_undo(before);
    view::stale_cache();
    char text[128];
    std::snprintf(text, sizeof(text), "Face flag 0x%04X %s on %d face%s", bit, on ? "set" : "cleared", faces,
                  faces == 1 ? "" : "s");
    hud::get_message(text);
    get_log("level editor: %s", text);
    return true;
}

bool home_selection()
{
    if (g_selection.empty())
        return false;
    std::shared_ptr<Step> before = get_capture(g_selection);
    int faces = 0;
    for (const FaceKey& key : g_selection) {
        auto origin = g_origins.find(key);
        uint16_t now = 0;
        if (origin != g_origins.end() && get_take_in(key, &now) && now != origin->second &&
            emit_out(key, origin->second))
            ++faces;
    }
    if (faces) {
        push_module_undo(before);
        view::stale_cache();
    }
    hud::get_message(faces ? "Face flags reverted" : "Face flags already original");
    return true;
}

std::vector<SectorFaces> get_changed()
{
    std::vector<Change> changes;
    for (const auto& origin : g_origins) {
        uint16_t now = 0;
        if (get_take_in(origin.first, &now))
            changes.push_back({origin.first, origin.second, now});
    }
    return group_set(changes);
}

int get_apply(const std::vector<SectorFaces>& sectors)
{
    int count = 0, missing = 0;
    for (const SectorFaces& s : sectors) {
        for (const auto& face : s.faces) {
            if (emit_out({s.checksum, face.first}, face.second))
                ++count;
            else
                ++missing;
        }
    }
    if (count)
        view::stale_cache();
    if (missing)
        get_log("level editor: %d edited collision faces aren't in this level's collision - skipped", missing);
    return count;
}

void reset_all()
{
    for (const auto& origin : g_origins) {
        uint16_t now = 0;
        if (get_take_in(origin.first, &now) && now != origin.second)
            emit_out(origin.first, origin.second);
    }
    g_selection.clear();
    view::stale_cache();
}

void forget_level()
{
    g_origins.clear();
    g_selection.clear();
    g_have_hover = false;
    g_queue.clear();
}

void tick_frame()
{
    std::vector<std::function<void()>> queue;
    queue.swap(g_queue);
    for (auto& action : queue)
        action();
    // (faces of a level gone: dropped from the selection)
    g_selection.erase(std::remove_if(g_selection.begin(), g_selection.end(),
                                     [](const FaceKey& key) {
                                         uint16_t flags = 0;
                                         return !get_take_in(key, &flags);
                                     }),
                      g_selection.end());
}

void add_overlay(V3 camera_pos, const TickCycle& frame, const Lens& lens, const uint32_t viewport[4], float near_z,
                 std::vector<ui::Seg>* lines)
{
    auto outline = [&](FaceKey key, uint32_t argb, float thickness) {
        V3 p[3];
        if (!get_corners(key, p))
            return;
        for (int k = 0; k < 3; ++k) {
            V3 a = p[k], b = p[(k + 1) % 3];
            if (!clip_to_near(camera_pos, frame, near_z + 0.01f, &a, &b))
                continue;
            float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f, nx = 0.0f, ny = 0.0f;
            if (!cast_onto(camera_pos, frame, lens, a, near_z, &nx, &ny))
                continue;
            ndc_to_pixel(viewport, nx, ny, &ax, &ay);
            if (!cast_onto(camera_pos, frame, lens, b, near_z, &nx, &ny))
                continue;
            ndc_to_pixel(viewport, nx, ny, &bx, &by);
            lines->push_back({ax, ay, bx, by, argb, thickness});
        }
    };
    size_t drawn = 0;
    for (const FaceKey& key : g_selection) {
        if (++drawn > 4096)
            break;
        outline(key, 0xFF40F0FF, 2.0f);
    }
    if (g_picking && g_have_hover)
        outline(g_hover, 0xFFFFF040, 2.5f);
}

std::string describe()
{
    char text[96];
    std::snprintf(text, sizeof(text), "Faces mode%s: %u selected", g_picking ? "" : " off",
                  static_cast<unsigned>(g_selection.size()));
    return text;
}

void inspector_section(ImFont* mono, ImFont* small_font)
{
    if (!g_picking && g_selection.empty())
        return;
    ImGui::Spacing();
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Collision faces###faceflags", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::PushFont(small_font);
    ImGui::TextDisabled("%s", g_picking ? "Click a face to select, Shift+click to add" : "Face Select is off");
    ImGui::PopFont();
    bool picking = g_picking;
    if (ImGui::Checkbox("Face Select Mode", &picking))
        g_queue.emplace_back([picking] { set_picking(picking); });
    if (g_selection.empty()) {
        ImGui::TextDisabled("No faces selected");
        return;
    }
    std::vector<uint16_t> flags;
    std::vector<uint32_t> pieces;
    for (const FaceKey& key : g_selection) {
        uint16_t f = 0;
        if (get_take_in(key, &f))
            flags.push_back(f);
        if (std::find(pieces.begin(), pieces.end(), key.checksum) == pieces.end())
            pieces.push_back(key.checksum);
    }
    ImGui::PushFont(mono);
    if (g_selection.size() == 1 && !flags.empty())
        ImGui::Text("face %u of %s: 0x%04X", static_cast<unsigned>(g_selection[0].face),
                    piece_label(g_selection[0].checksum).c_str(), flags[0]);
    else
        ImGui::Text("%u faces in %u piece%s", static_cast<unsigned>(g_selection.size()),
                    static_cast<unsigned>(pieces.size()), pieces.size() == 1 ? "" : "s");
    ImGui::PopFont();
    for (int i = 0; i < kFlagCount; ++i) {
        const FlagInfo& info = kFlags[i];
        int state = get_combined(flags, info.bit);
        bool on = state == 1;
        char label[64];
        std::snprintf(label, sizeof(label), "%s%s##face%d", info.name, state == 2 ? " (some)" : "", i);
        if (ImGui::Checkbox(label, &on)) {
            uint16_t bit = info.bit;
            bool set = state != 1; // (mixed or clear: set on all; all set: clear)
            g_queue.emplace_back([bit, set] { set_flag(bit, set); });
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", info.what);
    }
    if (ImGui::Button("Home (the level's flags)##faces"))
        g_queue.emplace_back([] { home_selection(); });
    ImGui::SameLine();
    if (ImGui::Button("Drop selection##faces"))
        g_queue.emplace_back([] { clear_selection(); });
    uint32_t piece = g_selection.back().checksum;
    std::string all_label = "All faces of " + piece_label(piece);
    if (ImGui::Button(all_label.c_str()))
        g_queue.emplace_back([piece] {
            if (select_piece(piece, false))
                hud::get_message("Selected all faces of " + piece_label(piece));
        });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Select all faces of this piece");
    if (scenery::has_selection()) {
        ImGui::SameLine();
        if (ImGui::Button("Select All Faces"))
            g_queue.emplace_back([] {
                std::vector<uint32_t> sectors = scenery::get_selection();
                bool any = false;
                for (unsigned int sector : sectors)
                    any = select_piece(sector, any) || any;
                hud::get_message(any ? "Selected all faces" : "Selection has no collision");
            });
    }
}

} // namespace frontend::level_editor::faces
