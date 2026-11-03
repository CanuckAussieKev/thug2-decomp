// NOT ORIGINAL - the level editor's collision face flags (ours). See level_editor_faces.h.

#include "editor/level_editor_faces.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <array>
#include <vector>

#include "engine/render/coll_obj.h"
#include "engine/core/enum_ops.h"
#include "editor/level_editor.h"
#include "editor/level_editor_actions.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_shared_coll.h"
#include "editor/level_editor_ui.h"
#include "editor/level_editor_view.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_static.h"
#include "engine/render/coll_tri.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "engine/gfx/sector.h"
#include "engine/core/ints.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "editor/level_editor_gui_text.h"
#include "engine/core/no_destroy.h"
#include "engine/core/checksums.h"

namespace frontend::level_editor::faces {

// --- the pure parts -------------------------------------------------------------------------------------------------

namespace {

constexpr int kHexLetterBase = 10; // the value of the hex digit 'a'
constexpr std::size_t kFaceTextSize = 32;
constexpr std::size_t kChecksumDigits = 8; // hex digits of a checksum
constexpr uint32_t kFaceOverflowGuard = 6553; // above this a decimal face number would pass 0xFFFF with one more digit
constexpr uint32_t kDecimalBase = 10;
constexpr uint32_t kMaxFaceNumber = 0xFFFF; // the highest face index of a collision object
constexpr int kMaxFaceIndex = 0xFFFF;
constexpr std::size_t kListTextSize = 160;
constexpr std::size_t kTipTextSize = 128;
constexpr std::size_t kMaxOutlinedFaces = 4096; // outlines drawn at most
constexpr uint32_t kSelectedOutlineColour = 0xFF40F0FF; // ARGB
constexpr float kSelectedOutlineWidth = 2.0f;
constexpr uint32_t kHoverOutlineColour = 0xFFFFF040; // ARGB
constexpr float kHoverOutlineWidth = 2.5f;
constexpr std::size_t kLabelTextSize = 96;
constexpr std::size_t kButtonLabelSize = 64;

const std::array<FlagInfo, kFlagCount> kFlags = {{
    {.bit = render::FaceFlag::kSkatable, .name = "Skatable", .what = "Skatable at any slope"},
    {.bit = render::FaceFlag::kNotSkatable, .name = "Not Skatable", .what = "Not skatable unless Skatable or Vert is set"},
    {.bit = render::FaceFlag::kWall, .name = "Wallride", .what = "Wallride only"},
    {.bit = render::FaceFlag::kVert, .name = "Vert", .what = "Launches like a quarter pipe"},
    {.bit = render::FaceFlag::kNotCollidable, .name = "Not Collidable", .what = "Skater, camera and objects pass through"},
    {.bit = render::FaceFlag::kTrigger, .name = "Trigger", .what = "Runs its node's TriggerScript when crossed"},
    {.bit = render::FaceFlag::kCameraPasses, .name = "Camera Pass-Through", .what = "Camera passes through"},
    {.bit = render::FaceFlag::kNoTagging, .name = "Not Taggable", .what = "Can't be tagged with graffiti"},
}};

int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + kHexLetterBase;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + kHexLetterBase;
    return -1;
}

} // namespace

const FlagInfo& bit_mark(int index) { return core::at(kFlags, std::clamp(index, 0, kFlagCount - 1)); }

render::FaceFlag get_applied(render::FaceFlag flags, render::FaceFlag set, render::FaceFlag clear)
{
    return (flags | set) & ~clear;
}

int get_combined(const std::vector<render::FaceFlag>& flags, render::FaceFlag bit)
{
    if (flags.empty())
        return -1;
    size_t on = 0;
    for (const render::FaceFlag f : flags)
        on += core::any(f & bit) ? 1 : 0;
    return on == 0 ? 0 : on == flags.size() ? 1 : 2;
}

bool get_click(std::vector<FaceKey>* selection, FaceKey key, bool add)
{
    auto it = std::ranges::find(*selection, key);
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
    std::array<char, kFaceTextSize> text{};
    for (const SectorFaces& s : sectors) {
        if (s.faces.empty())
            continue;
        text::format_to(text, "faces %08X ", s.checksum);
        out += text.data();
        for (size_t i = 0; i < s.faces.size(); ++i) {
            text::format_to(text, "%s%u:%04X", i ? "," : "", static_cast<unsigned>(core::at(s.faces, i).first),
                            static_cast<unsigned>(core::to_underlying(core::at(s.faces, i).second)));
            out += text.data();
        }
        out += '\n';
    }
    return out;
}

bool read_record(std::istringstream& in, SectorFaces* out)
{
    SectorFaces s;
    std::string checksum, list;
    if (!(in >> checksum >> list) || checksum.size() != kChecksumDigits)
        return false;
    uint32_t value = 0;
    for (const char c : checksum) {
        const int d = hex_digit(c);
        if (d < 0)
            return false;
        value = value << 4 | static_cast<uint32_t>(d);
    }
    s.checksum = value;
    size_t at = 0;
    while (at <= list.size()) {
        const size_t comma = list.find(',', at);
        std::string item = list.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        const size_t colon = item.find(':');
        if (colon == std::string::npos || colon == 0 || colon + 1 >= item.size() || item.size() - colon - 1 > 4)
            return false;
        uint32_t face = 0;
        for (size_t i = 0; i < colon; ++i) {
            if (core::at(item, i) < '0' || core::at(item, i) > '9' || face > kFaceOverflowGuard)
                return false;
            face = face * kDecimalBase + static_cast<uint32_t>(core::at(item, i) - '0');
        }
        if (face > kMaxFaceNumber)
            return false;
        uint32_t flags = 0;
        for (size_t i = colon + 1; i < item.size(); ++i) {
            const int d = hex_digit(core::at(item, i));
            if (d < 0)
                return false;
            flags = flags << 4 | static_cast<uint32_t>(d);
        }
        for (const auto& f : s.faces)
            if (f.first == face)
                return false; // (a face twice: not a record we wrote)
        s.faces.emplace_back(static_cast<uint16_t>(face), static_cast<render::FaceFlag>(flags));
        if (comma == std::string::npos)
            break;
        at = comma + 1;
    }
    std::string more;
    if (s.faces.empty() || (in >> more))
        return false;
    std::ranges::sort(s.faces);
    *out = s;
    return true;
}

std::vector<SectorFaces> group_set(const std::vector<Change>& changes)
{
    std::map<uint32_t, std::vector<std::pair<uint16_t, render::FaceFlag>>> by_sector;
    for (const Change& c : changes) {
        if (c.now != c.origin)
            by_sector[c.key.checksum].emplace_back(c.key.face, c.now);
    }
    std::vector<SectorFaces> out;
    for (auto& entry : by_sector) {
        std::ranges::sort(entry.second);
        out.push_back({.checksum = entry.first, .faces = entry.second});
    }
    return out;
}

// --- the game side --------------------------------------------------------------------------------------------------

namespace {

constexpr size_t kMostSelected = 8192;

// NOT ORIGINAL grouping - level_editor_faces.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_faces_state()).
struct LevelEditorFacesState {
    bool picking_{};
    std::vector<FaceKey> selection_{};
    bool have_hover_{};
    FaceKey hover_{};
    std::vector<std::function<void()>> queue_{};
};

LevelEditorFacesState& level_editor_faces_state()
{
    static core::NoDestroy<LevelEditorFacesState> state;
    return state.get();
}

std::map<FaceKey, render::FaceFlag>& origins()
{
    static std::map<FaceKey, render::FaceFlag> value; // (each face the editor changed: its flags as the level has them)
    return value;
}

// The sector's collision geometry (its StaticCollider's), null for none.
render::TriMeshSource* get_geometry(uint32_t checksum)
{
    const render::WorldCell* sector = scenery::sector_of(checksum);
    const render::HitMesh* coll = sector ? sector->hit_body() : nullptr;
    if (!render::is_static_collider(coll))
        return nullptr;
    auto* tri = coll->geometry();
    return tri && tri->faces() ? tri : nullptr;
}

// The face's flags written (a taken face: in its companion - the original keeps 0x10); its level's flags noted first.
bool emit_out(FaceKey key, render::FaceFlag flags)
{
    render::TriMeshSource* tri = get_geometry(key.checksum);
    if (!tri || key.face >= tri->tri_count())
        return false;
    if (!origins().contains(key)) {
        render::FaceFlag origin{};
        if (!get_take_in(key, &origin))
            return false;
        origins()[key] = origin;
    }
    if (!shared_collision::set_taken_flags(tri, {.face = key.face, .flags = flags}))
        tri->set_face_flags(key.face, flags);
    return true;
}

// The face's corners where the skater meets it (a taken face: its companion's).
bool get_corners(FaceKey key, std::span<V3, 3> out)
{
    const render::TriMeshSource* tri = get_geometry(key.checksum);
    if (!tri || key.face >= tri->tri_count() || !tri->vertices())
        return false;
    uint32_t piece = 0;
    int index = -1;
    if (shared_collision::taken_face(tri, key.face, &piece, &index, nullptr)) {
        const shared_collision::Companion* companion = shared_collision::companion_of(piece);
        if (!companion || index >= companion->face_count())
            return false;
        for (int k = 0; k < 3; ++k) {
            const uint32_t v = companion->get_geometry().face_vertex(index, k);
            if (v >= ints::as_unsigned(companion->get_vertex_count()))
                return false;
            core::at(out, k) = companion->get_vertex(static_cast<int>(v));
        }
        return true;
    }
    for (int k = 0; k < 3; ++k) {
        const uint32_t v = tri->face_vertex(key.face, k);
        if (v >= tri->num_vertices())
            return false;
        maths::Vec4 p{};
        tri->read_stored_vertex(static_cast<int>(v), &p);
        core::at(out, k) = {.x = p.x, .y = p.y, .z = p.z};
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
        if (!shared_collision::origin_of(checksums::Name{hit.checksum}, hit.face, &geometry, &face) || !geometry) {
            *why = "Face belongs to a copy; edit the source";
            return false;
        }
        checksum = geometry->checksum();
    }
    if (scenery::is_clone(checksum)) {
        *why = "Face belongs to a copy; edit the source";
        return false;
    }
    if (!get_geometry(checksum) || face < 0 || face > kMaxFaceIndex) {
        *why = "Not a level collision face";
        return false;
    }
    *key = {.checksum = checksum, .face = static_cast<uint16_t>(face)};
    return true;
}

struct Step : ModuleStep {
    std::vector<std::pair<FaceKey, render::FaceFlag>> flags;
    std::vector<FaceKey> selection;
    void get_restore() override
    {
        for (const auto& f : flags)
            emit_out(f.first, f.second);
        level_editor_faces_state().selection_ = selection;
        view::stale_cache();
    }
    [[nodiscard]] std::shared_ptr<ModuleStep> get_now() const override
    {
        auto now = std::make_shared<Step>();
        for (const auto& f : flags) {
            render::FaceFlag value = f.second;
            get_take_in(f.first, &value);
            now->flags.emplace_back(f.first, value);
        }
        now->selection = level_editor_faces_state().selection_;
        return now;
    }
};

std::shared_ptr<Step> get_capture(const std::vector<FaceKey>& keys)
{
    auto step = std::make_shared<Step>();
    for (const FaceKey& key : keys) {
        render::FaceFlag value{};
        if (get_take_in(key, &value))
            step->flags.emplace_back(key, value);
    }
    step->selection = level_editor_faces_state().selection_;
    return step;
}

std::string piece_label(uint32_t checksum) { return object_edit::caption(checksum); }

} // namespace

bool get_picking() { return level_editor_faces_state().picking_; }

void set_picking(bool on)
{
    // The collision view comes on with Face Select and goes off with it again if Face Select turned it on.
    static bool s_turned_collision_on;
    if (on && !level_editor_faces_state().picking_) {
        s_turned_collision_on = !view::show_collision();
        if (s_turned_collision_on)
            view::set_show_collision(true);
    } else if (!on && level_editor_faces_state().picking_) {
        if (s_turned_collision_on && view::show_collision())
            view::set_show_collision(false);
        s_turned_collision_on = false;
    }
    level_editor_faces_state().picking_ = on;
    if (!on)
        level_editor_faces_state().have_hover_ = false;
}

void kickoff()
{
    actions::set_checked(actions::kFaces, [] { return level_editor_faces_state().picking_ ? 1 : 0; }); // (its check in the View menu)
    level_editor_faces_state().picking_ = false;
    level_editor_faces_state().selection_.clear();
    level_editor_faces_state().have_hover_ = false;
}

void halt()
{
    level_editor_faces_state().picking_ = false;
    level_editor_faces_state().selection_.clear();
    level_editor_faces_state().have_hover_ = false;
    level_editor_faces_state().queue_.clear();
}

const std::vector<FaceKey>& get_selection() { return level_editor_faces_state().selection_; }
bool has_selection() { return !level_editor_faces_state().selection_.empty(); }
void clear_selection() { level_editor_faces_state().selection_.clear(); }

bool click_at(V3 origin, V3 unit_dir, bool add)
{
    view::FaceHit hit;
    if (!view::pick_face(origin, unit_dir, &hit)) {
        if (!add)
            level_editor_faces_state().selection_.clear();
        hud::get_message("No collision face under cursor");
        return false;
    }
    FaceKey key{};
    std::string why;
    if (!get_key_of(hit, &key, &why)) {
        hud::get_message(why, hud::kShortMessageSeconds);
        return false;
    }
    if (add && level_editor_faces_state().selection_.size() >= kMostSelected && std::ranges::find(level_editor_faces_state().selection_, key) == level_editor_faces_state().selection_.end()) {
        hud::get_message("Limit: 8192 faces");
        return false;
    }
    const bool selected = get_click(&level_editor_faces_state().selection_, key, add);
    render::FaceFlag flags{};
    get_take_in(key, &flags);
    std::array<char, kListTextSize> text{};
    text::format_to(text, "Face %u of %s %s - flags 0x%04X (%u selected)", static_cast<unsigned>(key.face),
                    piece_label(key.checksum).c_str(), selected ? "selected" : "dropped", core::to_underlying(flags),
                    static_cast<unsigned>(level_editor_faces_state().selection_.size()));
    hud::get_message(text.data());
    if (debug_enabled())
        get_log("level editor: %s", text.data());
    return true;
}

void get_hover(V3 origin, V3 unit_dir, bool have_ray)
{
    level_editor_faces_state().have_hover_ = false;
    view::FaceHit hit;
    std::string why;
    if (level_editor_faces_state().picking_ && have_ray && view::pick_face(origin, unit_dir, &hit) && get_key_of(hit, &level_editor_faces_state().hover_, &why))
        level_editor_faces_state().have_hover_ = true;
}

bool select_piece(uint32_t checksum, bool add)
{
    std::vector<FaceKey> faces;
    if (scenery::is_clone(checksum))
        return false;
    if (const render::TriMeshSource* tri = get_geometry(checksum)) {
        for (int f = 0; std::cmp_less(f, tri->tri_count()) && faces.size() < kMostSelected; ++f)
            faces.push_back({.checksum = checksum, .face = static_cast<uint16_t>(f)});
    }
    // (the faces its shared collision took along: theirs to edit too)
    if (const shared_collision::Companion* companion = shared_collision::companion_of(checksum)) {
        for (int i = 0; i < companion->face_count() && faces.size() < kMostSelected; ++i) {
            render::TriMeshSource* geometry = nullptr;
            int face = -1;
            if (shared_collision::origin_of(checksums::Name{checksum}, i, &geometry, &face) && geometry && face >= 0)
                faces.push_back({.checksum = geometry->checksum(), .face = static_cast<uint16_t>(face)});
        }
    }
    if (faces.empty())
        return false;
    if (!add)
        level_editor_faces_state().selection_.clear();
    for (const FaceKey& key : faces) {
        if (level_editor_faces_state().selection_.size() >= kMostSelected)
            break;
        if (std::ranges::find(level_editor_faces_state().selection_, key) == level_editor_faces_state().selection_.end())
            level_editor_faces_state().selection_.push_back(key);
    }
    return true;
}

bool get_take_in(FaceKey key, render::FaceFlag* flags)
{
    const render::TriMeshSource* tri = get_geometry(key.checksum);
    if (!tri || key.face >= tri->tri_count())
        return false;
    if (!shared_collision::taken_face(tri, key.face, nullptr, nullptr, flags))
        *flags = tri->face_flags(key.face);
    return true;
}

bool set_flag(render::FaceFlag bit, bool on)
{
    if (level_editor_faces_state().selection_.empty())
        return false;
    const std::shared_ptr<Step> before = get_capture(level_editor_faces_state().selection_);
    bool changed = false;
    int faces = 0;
    for (const FaceKey& key : level_editor_faces_state().selection_) {
        render::FaceFlag flags{};
        if (!get_take_in(key, &flags))
            continue;
        const render::FaceFlag now = on ? get_applied(flags, bit, render::kNoFaceFlags) : get_applied(flags, render::kNoFaceFlags, bit);
        if (now != flags && emit_out(key, now)) {
            changed = true;
            ++faces;
        }
    }
    if (!changed)
        return false;
    push_module_undo(before);
    view::stale_cache();
    std::array<char, kTipTextSize> text{};
    text::format_to(text, "Face flag 0x%04X %s on %d face%s", core::to_underlying(bit), on ? "set" : "cleared", faces,
                    faces == 1 ? "" : "s");
    hud::get_message(text.data());
    get_log("level editor: %s", text.data());
    return true;
}

bool home_selection()
{
    if (level_editor_faces_state().selection_.empty())
        return false;
    const std::shared_ptr<Step> before = get_capture(level_editor_faces_state().selection_);
    int faces = 0;
    for (const FaceKey& key : level_editor_faces_state().selection_) {
        auto origin = origins().find(key);
        render::FaceFlag now{};
        if (origin != origins().end() && get_take_in(key, &now) && now != origin->second &&
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
    for (const auto& origin : origins()) {
        render::FaceFlag now{};
        if (get_take_in(origin.first, &now))
            changes.push_back({.key = origin.first, .origin = origin.second, .now = now});
    }
    return group_set(changes);
}

int get_apply(const std::vector<SectorFaces>& sectors)
{
    int count = 0, missing = 0;
    for (const SectorFaces& s : sectors) {
        for (const auto& face : s.faces) {
            if (emit_out({.checksum = s.checksum, .face = face.first}, face.second))
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
    for (const auto& origin : origins()) {
        render::FaceFlag now{};
        if (get_take_in(origin.first, &now) && now != origin.second)
            emit_out(origin.first, origin.second);
    }
    level_editor_faces_state().selection_.clear();
    view::stale_cache();
}

void forget_level()
{
    origins().clear();
    level_editor_faces_state().selection_.clear();
    level_editor_faces_state().have_hover_ = false;
    level_editor_faces_state().queue_.clear();
}

void tick_frame()
{
    std::vector<std::function<void()>> queue;
    queue.swap(level_editor_faces_state().queue_);
    for (auto& action : queue)
        action();
    // (faces of a level gone: dropped from the selection)
    std::erase_if(level_editor_faces_state().selection_, [](const FaceKey& key) {
        render::FaceFlag flags{};
        return !get_take_in(key, &flags);
    });
}

void add_overlay(V3 camera_pos, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport, float near_z,
                 std::vector<ui::Seg>* lines)
{
    auto outline = [&](FaceKey key, uint32_t argb, float thickness) {
        std::array<V3, 3> p{};
        if (!get_corners(key, p))
            return;
        for (int k = 0; k < 3; ++k) {
            V3 a = core::at(p, k), b = core::at(p, (k + 1) % 3);
            if (!clip_to_near(camera_pos, frame, near_z + kNearPlaneMargin, &a, &b))
                continue;
            float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f, nx = 0.0f, ny = 0.0f;
            if (!cast_onto(camera_pos, frame, lens, a, near_z, &nx, &ny))
                continue;
            ndc_to_pixel(viewport, nx, ny, &ax, &ay);
            if (!cast_onto(camera_pos, frame, lens, b, near_z, &nx, &ny))
                continue;
            ndc_to_pixel(viewport, nx, ny, &bx, &by);
            lines->push_back({.ax = ax, .ay = ay, .bx = bx, .by = by, .argb = argb, .thickness = thickness});
        }
    };
    size_t drawn = 0;
    for (const FaceKey& key : level_editor_faces_state().selection_) {
        if (++drawn > kMaxOutlinedFaces)
            break;
        outline(key, kSelectedOutlineColour, kSelectedOutlineWidth);
    }
    if (level_editor_faces_state().picking_ && level_editor_faces_state().have_hover_)
        outline(level_editor_faces_state().hover_, kHoverOutlineColour, kHoverOutlineWidth);
}

std::string describe()
{
    std::array<char, kLabelTextSize> text{};
    text::format_to(text, "Faces mode%s: %u selected", level_editor_faces_state().picking_ ? "" : " off",
                    static_cast<unsigned>(level_editor_faces_state().selection_.size()));
    return text.data();
}

void inspector_section(ImFont* mono, ImFont* small_font)
{
    if (!level_editor_faces_state().picking_ && level_editor_faces_state().selection_.empty())
        return;
    ImGui::Spacing();
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Collision faces###faceflags", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::PushFont(small_font);
    gui::text_disabled("%s", level_editor_faces_state().picking_ ? "Click a face to select, Shift+click to add" : "Face Select is off");
    ImGui::PopFont();
    bool picking = level_editor_faces_state().picking_;
    if (ImGui::Checkbox("Face Select Mode", &picking))
        level_editor_faces_state().queue_.emplace_back([picking] { set_picking(picking); });
    if (level_editor_faces_state().selection_.empty()) {
        gui::text_disabled("No faces selected");
        return;
    }
    std::vector<render::FaceFlag> flags;
    std::vector<uint32_t> pieces;
    for (const FaceKey& key : level_editor_faces_state().selection_) {
        render::FaceFlag f{};
        if (get_take_in(key, &f))
            flags.push_back(f);
        if (std::ranges::find(pieces, key.checksum) == pieces.end())
            pieces.push_back(key.checksum);
    }
    ImGui::PushFont(mono);
    if (level_editor_faces_state().selection_.size() == 1 && !flags.empty())
        gui::text("face %u of %s: 0x%04X", static_cast<unsigned>(core::at(level_editor_faces_state().selection_, 0).face),
                    piece_label(core::at(level_editor_faces_state().selection_, 0).checksum).c_str(), core::to_underlying(core::at(flags, 0)));
    else
        gui::text("%u faces in %u piece%s", static_cast<unsigned>(level_editor_faces_state().selection_.size()),
                    static_cast<unsigned>(pieces.size()), pieces.size() == 1 ? "" : "s");
    ImGui::PopFont();
    for (int i = 0; i < kFlagCount; ++i) {
        const FlagInfo& info = core::at(kFlags, i);
        const int state = get_combined(flags, info.bit);
        bool on = state == 1;
        std::array<char, kButtonLabelSize> label{};
        text::format_to(label, "%s%s##face%d", info.name, state == 2 ? " (some)" : "", i);
        if (ImGui::Checkbox(label.data(), &on)) {
            const render::FaceFlag bit = info.bit;
            const bool set = state != 1; // (mixed or clear: set on all; all set: clear)
            level_editor_faces_state().queue_.emplace_back([bit, set] { set_flag(bit, set); });
        }
        if (ImGui::IsItemHovered())
            gui::tooltip("%s", info.what);
    }
    if (ImGui::Button("Home (the level's flags)##faces"))
        level_editor_faces_state().queue_.emplace_back([] { home_selection(); });
    ImGui::SameLine();
    if (ImGui::Button("Drop selection##faces"))
        level_editor_faces_state().queue_.emplace_back([] { clear_selection(); });
    const uint32_t piece = level_editor_faces_state().selection_.back().checksum;
    const std::string all_label = "All faces of " + piece_label(piece);
    if (ImGui::Button(all_label.c_str()))
        level_editor_faces_state().queue_.emplace_back([piece] {
            if (select_piece(piece, false))
                hud::get_message("Selected all faces of " + piece_label(piece));
        });
    if (ImGui::IsItemHovered())
        gui::tooltip("Select all faces of this piece");
    if (scenery::has_selection()) {
        ImGui::SameLine();
        if (ImGui::Button("Select All Faces"))
            level_editor_faces_state().queue_.emplace_back([] {
                // (a copy: selecting changes the selection)
                const auto sectors = std::vector<uint32_t>(scenery::get_selection());
                bool any = false;
                for (const unsigned int sector : sectors)
                    any = select_piece(sector, any) || any;
                hud::get_message(any ? "Selected all faces" : "Selection has no collision");
            });
    }
}

} // namespace frontend::level_editor::faces
