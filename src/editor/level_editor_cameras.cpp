// NOT ORIGINAL - the level editor's camera nodes (ours). See level_editor_cameras.h.

#include "editor/level_editor_cameras.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <vector>
#include <array>

#include "editor/level_editor.h"
#include "editor/level_editor_fields.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_ui.h"
#include "engine/maths/vec4.h"
#include "engine/objects/movie_cfuncs.h"
#include "engine/script/node_manager.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include "engine/core/format.h"
#include "editor/level_editor_gui_text.h"
#include "engine/core/no_destroy.h"
#include "engine/core/checksums.h"
#include "engine/maths/trig.h"
#include "editor/level_editor_ui_layout.h"
#include "editor/level_editor_ui_theme.h"

namespace frontend::level_editor::cameras {

namespace {

constexpr float kFrameRate = 60.0f;             // frames a second the shot timings count in
constexpr std::size_t kLineTextSize = 320;
constexpr double kFrameRateDouble = 60.0;
constexpr double kFailureMessageSeconds = 4.0;
constexpr float kMaxFrustumReach = 12000.0f;    // a camera further than this from the view is not drawn
constexpr float kFrustumDepth = 120.0f;         // the drawn frustum's depth
constexpr float kNearWeight = 0.75f;            // a marker point three quarters of the way from one corner to the other
constexpr float kFarWeight = 0.25f;
constexpr uint32_t kLitColour = 0xC0FFF060;     // ARGB: the line to the target of the camera in use
constexpr uint32_t kUnlitColour = 0x60FF9030;   // ARGB: ... of the others
constexpr float kFieldWidth = 160.0f;           // the Pitch and Target fields
constexpr float kPitchDragSpeed = 0.25f;
constexpr float kTargetDragSpeed = 5.0f;


} // namespace

// --- the pure parts -------------------------------------------------------------------------------------------------

namespace {


// Numbers separated by commas, the whole text (as the layout's other records read them).
bool read_numbers(const std::string& text, std::span<float> out, int count)
{
    const char* at = text.c_str();
    for (int i = 0; i < count; ++i) {
        char* end = nullptr;
        core::at(out, i) = std::strtof(at, &end);
        if (end == at || !std::isfinite(core::at(out, i)))
            return false;
        at = end;
        if (i + 1 < count) {
            if (*at != ',')
                return false;
            at = std::next(at);
        }
    }
    return *at == '\0';
}

bool read_triple(const std::string& text, V3* out)
{
    std::array<float, 3> v{};
    if (!read_numbers(text, v, 3))
        return false;
    *out = {.x = core::at(v, 0), .y = core::at(v, 1), .z = core::at(v, 2)};
    return true;
}

} // namespace

float clamp_distance(float d)
{
    if (!(d == d))
        return kDefaultDistance;
    return std::clamp(d, kMinDistance, kMaxDistance);
}

// The game's angles-to-matrix (0x0041A730): the identity times RotationX(x), RotationY(y), RotationZ(z) (D3DX's, row
// vectors) - its at row (sin y cos x, -sin x, cos y cos x) for z 0. The editor's view looks along (sin yaw cos pitch,
// sin pitch, cos yaw cos pitch): x = -pitch, y = yaw.
V3 angles_for(float yaw, float pitch) { return {.x = -pitch, .y = yaw, .z = 0.0f}; }

void yaw_pitch_of(V3 angles, YawPitchOut view)
{
    auto* const yaw = view.yaw;
    auto* const pitch = view.pitch;
    *yaw = angles.y;
    *pitch = -angles.x;
}

V3 get_forward(V3 angles)
{
    // (RotationZ last: it turns that row about the world's z axis - the editor's cameras keep z 0)
    const float cx = std::cos(angles.x);
    const float sx = std::sin(angles.x);
    const V3 v = {.x = std::sin(angles.y) * cx, .y = -sx, .z = std::cos(angles.y) * cx};
    const float cz = std::cos(angles.z);
    const float sz = std::sin(angles.z);
    return {.x = (v.x * cz) - (v.y * sz), .y = (v.x * sz) + (v.y * cz), .z = v.z};
}

V3 target_of(V3 pos, V3 angles, float distance) { return pos + get_forward(angles) * clamp_distance(distance); }

V3 position_offset(V3 pos, V3 angles, float distance) { return pos - target_of(pos, angles, distance); }

int shot_frames(float seconds)
{
    if (!std::isfinite(seconds))
        seconds = kShotDefaultSeconds;
    seconds = std::max(kShotMinSeconds, std::min(kShotMaxSeconds, seconds));
    return static_cast<int>(std::lround(seconds * kFrameRate));
}

void frustum_corners(CameraPlace camera_place, float tan_x, float tan_y, float depth, std::span<V3, 4> out)
{
    const auto pos = camera_place.pos;
    const auto angles = camera_place.angles;
    float yaw = 0.0f;
    float pitch = 0.0f;
    yaw_pitch_of(angles, {.yaw = &yaw, .pitch = &pitch});
    const TickCycle f = frame_from_yaw_pitch(yaw, pitch);
    const V3 centre = pos - f.back * depth;
    const V3 right = f.right * (tan_x * depth);
    const V3 up = f.up * (tan_y * depth);
    core::at(out, 0) = centre + right + up;
    core::at(out, 1) = centre - right + up;
    core::at(out, 2) = centre - right - up;
    core::at(out, 3) = centre + right - up;
}

bool get_same(const ViewCam& a, const ViewCam& b)
{
    auto same = [](V3 p, V3 q) { return p.x == q.x && p.y == q.y && p.z == q.z; };
    return a.name == b.name && same(a.pos, b.pos) && same(a.angles, b.angles) && a.distance == b.distance;
}

std::string write_record(const ViewCam& c)
{
    std::array<char, kLineTextSize> text{};
    text::format_to(text, "camera %s pos=%.9g,%.9g,%.9g angles=%.9g,%.9g,%.9g dist=%.9g", c.name.c_str(), c.pos.x,
                    c.pos.y, c.pos.z, c.angles.x, c.angles.y, c.angles.z, c.distance);
    return text.data();
}

bool read_record(std::istringstream& in, ViewCam* out)
{
    ViewCam c;
    if (!(in >> c.name) || !node_create::valid_name(c.name))
        return false;
    bool have_pos = false;
    bool have_angles = false;
    bool have_distance = false;
    std::string field;
    while (in >> field) {
        const size_t equals = field.find('=');
        if (equals == std::string::npos || equals == 0)
            return false;
        const std::string key = field.substr(0, equals);
        const std::string value = field.substr(equals + 1);
        if (key == "pos") {
            if (have_pos || !read_triple(value, &c.pos))
                return false;
            have_pos = true;
        } else if (key == "angles") {
            if (have_angles || !read_triple(value, &c.angles))
                return false;
            have_angles = true;
        } else if (key == "dist") {
            if (have_distance || !read_numbers(value, std::span<float>(&c.distance, 1), 1))
                return false;
            c.distance = clamp_distance(c.distance);
            have_distance = true;
        } // (another key: a later version's - skipped)
    }
    if (!have_pos)
        return false;
    *out = c;
    return true;
}

// --- the game side --------------------------------------------------------------------------------------------------

namespace {

std::map<std::string, float>& distances_state()
{
    static std::map<std::string, float> value; // (each camera's target distance, by name)
    return value;
}
// NOT ORIGINAL grouping - level_editor_cameras.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_cameras_state()).
struct LevelEditorCamerasState {
    std::vector<std::function<void()>> queue_;
    // A field being dragged (pitch / distance): the cameras before its first change (one undo step).
    bool dragging_{};
};

LevelEditorCamerasState& level_editor_cameras_state()
{
    static core::NoDestroy<LevelEditorCamerasState> state;
    return state.get();
}

bool same_text(const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) == 0; }

float distance_of(const std::string& name)
{
    auto it = distances_state().find(name);
    return it == distances_state().end() ? kDefaultDistance : it->second;
}

node_create::Spec spec_of(const ViewCam& c)
{
    node_create::Spec n;
    n.name = c.name;
    n.cls = "GenericNode";
    n.type = "ed_camera";
    n.pos = c.pos;
    n.angles = c.angles;
    auto vector = [&](const char* key, V3 v) {
        node_create::Prop f = {.type = node_create::Prop::kVector, .key = key};
        core::at(f.value, 0) = v.x;
        core::at(f.value, 1) = v.y;
        core::at(f.value, 2) = v.z;
        n.extra.push_back(f);
    };
    vector("targetOffset", target_of(c.pos, c.angles, c.distance));
    vector("positionOffset", position_offset(c.pos, c.angles, c.distance));
    n.owned = true;
    return n;
}

// The whole state (the cameras and their distances) - an undo step puts it back.
struct Step : ModuleStep {
    std::vector<ViewCam> cameras;
    void get_restore() override
    {
        const std::vector<ViewCam> now = get_all();
        // (those not in the step: removed; those in it: made, or put where the step has them)
        for (const ViewCam& c : now) {
            const bool keep = std::ranges::any_of(cameras, [&](const ViewCam& k) { return same_text(k.name, c.name); });
            if (!keep) {
                node_create::Spec removed;
                int at = -1;
                node_create::drop_out(node_create::name_hash(c.name), &removed, &at);
            }
        }
        std::string message;
        for (const ViewCam& c : cameras) {
            distances_state()[c.name] = c.distance;
            const uint32_t name = node_create::name_hash(c.name);
            if (!node_create::is_added(name)) {
                node_create::fabricate(spec_of(c), -1, &message);
            } else {
                node_edit::Values values;
                values.pos = c.pos;
                values.angles = c.angles;
                node_edit::assign_value(name, values);
            }
        }
    }
    [[nodiscard]] std::shared_ptr<ModuleStep> get_now() const override
    {
        auto now = std::make_shared<Step>();
        now->cameras = get_all();
        return now;
    }
};

void push_undo()
{
    auto step = std::make_shared<Step>();
    step->cameras = get_all();
    push_module_undo(step);
}

const ViewCam* get_find(const std::vector<ViewCam>& all, uint32_t name)
{
    for (const ViewCam& c : all)
        if (node_create::name_hash(c.name) == name)
            return &c;
    return nullptr;
}

// The node's offsets written when they're not those of its place (it was moved / turned since).
void keep_offsets(const ViewCam& c)
{
    script::Struct* node = level_nodes::node_manager_instance()
                               ? level_nodes::fetch_vertex(node_create::name_hash(c.name), false)
                               : nullptr;
    if (!node)
        return;
    struct Key {
        uint32_t key;
        V3 value;
    };
    const std::array<Key, 2> keys = {{{.key = fields::hash_of_name("targetOffset"), .value = target_of(c.pos, c.angles, c.distance)},
                         {.key = fields::hash_of_name("positionOffset"), .value = position_offset(c.pos, c.angles, c.distance)}}};
    for (const Key& k : keys) {
        const fields::Own own = fields::own_in(node, k.key, false);
        const bool same = own.present && own.value.kind == fields::Kind::kVec4 && core::at(own.value.numbers, 0) == k.value.x &&
                          core::at(own.value.numbers, 1) == k.value.y && core::at(own.value.numbers, 2) == k.value.z;
        if (same)
            continue;
        fields::Own put = own;
        put.present = true;
        put.value = fields::Value{};
        put.value.kind = fields::Kind::kVec4;
        core::at(put.value.numbers, 0) = k.value.x;
        core::at(put.value.numbers, 1) = k.value.y;
        core::at(put.value.numbers, 2) = k.value.z;
        fields::put_in(node, k.key, false, put);
    }
}

bool look_through(const ViewCam& c)
{
    float yaw = 0.0f;
    float pitch = 0.0f;
    yaw_pitch_of(c.angles, {.yaw = &yaw, .pitch = &pitch});
    look_from(c.pos, {.yaw = yaw, .pitch = pitch});
    hud::get_message("Viewing through " + c.name);
    return true;
}

} // namespace

std::vector<ViewCam> get_all()
{
    std::vector<ViewCam> out;
    for (const node_create::Spec& spec : node_create::at_hand()) {
        if (!same_text(spec.type, "ed_camera") || !same_text(spec.cls, "GenericNode"))
            continue;
        ViewCam c;
        c.name = spec.name;
        c.pos = spec.pos;
        c.angles = spec.angles;
        node_edit::Values values;
        if (node_edit::get_take_in(node_create::name_hash(spec.name), &values)) {
            c.pos = values.pos;
            c.angles = values.angles;
        }
        c.distance = distance_of(spec.name);
        out.push_back(c);
    }
    return out;
}

namespace {

uint32_t get_checksums(const char* text) { return fields::hash_of_name(text); }

// When the last shot ends (steady clock seconds; 0: none played).
double& shot_until()
{
    static double shot_until{};
    return shot_until;
}
double get_now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

using Function = bool (*)(script::Struct*, script::Script*);

// A camera shot's offsets: its target's and its position's.
struct ShotOffsets {
    V3* target{};
    V3* position{};
};

// The offsets of the camera `name` (false: no such node).
bool shot_offsets(uint32_t name, ShotOffsets offsets)
{
    auto* const target = offsets.target;
    auto* const position = offsets.position;
    const std::vector<ViewCam> all = get_all();
    if (const ViewCam* c = get_find(all, name)) {
        *target = target_of(c->pos, c->angles, c->distance);
        *position = position_offset(c->pos, c->angles, c->distance);
        return true;
    }
    script::Struct* node = level_nodes::node_manager_instance() ? level_nodes::fetch_vertex(name, false) : nullptr;
    if (!node)
        return false;
    script::VectorValue t{};
    script::VectorValue p{};
    if (node->fetch_vec(get_checksums("targetOffset"), &t, 0) &&
        node->fetch_vec(get_checksums("positionOffset"), &p, 0)) {
        *target = {.x = t.x, .y = t.y, .z = t.z};
        *position = {.x = p.x, .y = p.y, .z = p.z};
        return true;
    }
    maths::Vec4 at;
    if (!level_nodes::fetch_place(node, &at))
        return false;
    script::VectorValue a = {};
    node->fetch_vec(get_checksums("Angles"), &a, 0);
    const V3 pos = {.x = at.x, .y = at.y, .z = at.z};
    const V3 angles = {.x = a.x, .y = a.y, .z = a.z};
    *target = target_of(pos, angles, kDefaultDistance);
    *position = position_offset(pos, angles, kDefaultDistance);
    return true;
}

} // namespace

bool cfunc_play_camera(script::Struct* params, script::Script* script)
{
    uint32_t name = 0;
    if (!params || !params->read_hash_value(get_checksums("camera"), &name, 0) || !name) {
        get_log("level editor: LevelEditor_PlayCamera needs camera=<node>");
        return false;
    }
    if (live_now())
        return false; // (the editor's own camera; Look through previews a camera there)
    V3 target{};
    V3 position{};
    if (!shot_offsets(name, {.target = &target, .position = &position})) {
        get_log("level editor: LevelEditor_PlayCamera - no node %s", nodes::caption(name).c_str());
        return false;
    }
    float seconds = kShotDefaultSeconds;
    int whole = 0;
    if (!params->fetch_float(get_checksums("time"), &seconds, 0) &&
        params->fetch_int_value(get_checksums("time"), &whole, 0))
        seconds = static_cast<float>(whole);
    const uint32_t shot = get_checksums("ed_camera_shot");
    const double now = get_now();
    if (now < shot_until()) {
        // (the last shot still playing: killed first, as the goals kill theirs)
        gsl::owner<script::Struct*> kill = script::new_struct();
        kill->put_name_key(checksums::Name{get_checksums("name")}, shot);
        commands::cfunc_kill_skater_cam_anim(kill, script);
        kill->get_clear();
        script::delete_struct(kill);
    }
    // The game's own virtual_cam, as view_goals_menu.qb's play_single_view_goals_cam plays a goal's camera.
    gsl::owner<script::Struct*> anim = script::new_struct();
    anim->put_name_key(checksums::Name{get_checksums("name")}, shot);
    anim->put_name_key(checksums::Name{0u}, get_checksums("virtual_cam"));
    anim->put_name_key(checksums::Name{get_checksums("targetid")}, get_checksums("world"));
    anim->put_vec3(checksums::Name{get_checksums("targetOffset")}, {.x = target.x, .y = target.y, .z = target.z});
    anim->put_vec3(checksums::Name{get_checksums("positionOffset")}, {.x = position.x, .y = position.y, .z = position.z});
    const int frames = shot_frames(seconds);
    anim->put_int(checksums::Name{get_checksums("frames")}, frames);
    anim->put_int(checksums::Name{get_checksums("skippable")}, 1);
    commands::cfunc_play_skater_cam_anim(anim, script);
    anim->get_clear();
    script::delete_struct(anim);
    shot_until() = now + (frames / kFrameRateDouble);
    if (debug_enabled())
        get_log("level editor: camera %s played for %d frames", nodes::caption(name).c_str(), frames);
    return true;
}

bool is_camera(uint32_t name)
{
    const std::vector<ViewCam> all = get_all();
    return get_find(all, name) != nullptr;
}

std::string create_from_view()
{
    ViewNow view{};
    if (!current_view(&view)) {
        hud::get_message("No view available");
        return "";
    }
    ViewCam c;
    c.name = node_create::fresh_name(kStem, [](const std::string& n) { return node_create::name_taken(n); });
    c.pos = view.pos;
    c.angles = angles_for(view.yaw, view.pitch);
    c.distance = kDefaultDistance;
    push_undo();
    std::string message;
    distances_state()[c.name] = c.distance;
    if (!node_create::fabricate(spec_of(c), -1, &message)) {
        hud::get_message(message.empty() ? "Couldn't create the camera" : message, kFailureMessageSeconds);
        return "";
    }
    hud::get_message("Added camera " + c.name);
    get_log("level editor: camera %s made at (%.1f, %.1f, %.1f), heading %.1f, pitch %.1f", c.name.c_str(), c.pos.x,
            c.pos.y, c.pos.z, view.yaw * kDegreesPerHalfTurn / maths::kPi, view.pitch * kDegreesPerHalfTurn / maths::kPi);
    return c.name;
}

bool get_preview(uint32_t name)
{
    const std::vector<ViewCam> all = get_all();
    const ViewCam* c = get_find(all, name);
    return c && look_through(*c);
}

bool aim_from_view(uint32_t name)
{
    const std::vector<ViewCam> all = get_all();
    const ViewCam* c = get_find(all, name);
    ViewNow view{};
    if (!c || !current_view(&view))
        return false;
    push_undo();
    node_edit::Values values;
    values.pos = view.pos;
    values.angles = angles_for(view.yaw, view.pitch);
    node_edit::note_origin(name);
    node_edit::assign_value(name, values);
    hud::get_message("Moved camera " + c->name + " to the view");
    return true;
}

bool set_pitch(checksums::Name camera, float degrees, bool done)
{
    const uint32_t name = camera.value();
    const std::vector<ViewCam> all = get_all();
    const ViewCam* c = get_find(all, name);
    if (!c)
        return false;
    if (!level_editor_cameras_state().dragging_)
        push_undo();
    level_editor_cameras_state().dragging_ = !done;
    node_edit::Values values;
    values.pos = c->pos;
    values.angles = c->angles;
    values.angles.x = -std::clamp(degrees, -kMaxPitchDegrees, kMaxPitchDegrees) * maths::kPi / kDegreesPerHalfTurn;
    node_edit::assign_value(name, values);
    return true;
}

bool place_at_range(checksums::Name camera, float distance, bool done)
{
    const uint32_t name = camera.value();
    const std::vector<ViewCam> all = get_all();
    const ViewCam* c = get_find(all, name);
    if (!c)
        return false;
    if (!level_editor_cameras_state().dragging_)
        push_undo();
    level_editor_cameras_state().dragging_ = !done;
    distances_state()[c->name] = clamp_distance(distance);
    return true;
}

bool drop_out(uint32_t name)
{
    const std::vector<ViewCam> all = get_all();
    const ViewCam* c = get_find(all, name);
    if (!c)
        return false;
    push_undo();
    node_create::Spec removed;
    int at = -1;
    const std::string label = c->name;
    if (!node_create::drop_out(name, &removed, &at))
        return false;
    hud::get_message("Deleted camera " + label);
    return true;
}

std::vector<ViewCam> get_capture() { return get_all(); }

int get_apply(const std::vector<ViewCam>& cameras)
{
    int made = 0;
    std::string message;
    for (const ViewCam& c : cameras) {
        distances_state()[c.name] = clamp_distance(c.distance);
        const uint32_t name = node_create::name_hash(c.name);
        if (node_create::is_added(name))
            continue;
        if (node_create::name_taken(c.name)) {
            get_log("level editor: camera %s - the level has a node of that name already: not made", c.name.c_str());
            continue;
        }
        if (node_create::fabricate(spec_of(c), -1, &message))
            ++made;
    }
    return made;
}

void reset_all()
{
    for (const ViewCam& c : get_all()) {
        node_create::Spec removed;
        int at = -1;
        node_create::drop_out(node_create::name_hash(c.name), &removed, &at);
    }
}

void forget_level()
{
    distances_state().clear();
    level_editor_cameras_state().queue_.clear();
    level_editor_cameras_state().dragging_ = false;
}

void tick_frame()
{
    std::vector<std::function<void()>> queue;
    queue.swap(level_editor_cameras_state().queue_);
    for (auto& action : queue)
        action();
    for (const ViewCam& c : get_all())
        keep_offsets(c);
}

void add_overlay(V3 camera_pos, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport, OverlayPick overlay_pick, std::vector<ui::Seg>* lines)
{
    const auto near_z = overlay_pick.near_z_;
    const auto selected = overlay_pick.selected;
    auto line = [&](V3 a, V3 b, uint32_t argb, float thickness) {
        if (!clip_to_near(camera_pos, frame, near_z + kNearPlaneMargin, &a, &b))
            return;
        float ax = 0.0f;
        float ay = 0.0f;
        float bx = 0.0f;
        float by = 0.0f;
        float nx = 0.0f;
        float ny = 0.0f;
        if (!cast_onto(camera_pos, frame, lens, a, near_z, &nx, &ny))
            return;
        ndc_to_pixel(viewport, nx, ny, &ax, &ay);
        if (!cast_onto(camera_pos, frame, lens, b, near_z, &nx, &ny))
            return;
        ndc_to_pixel(viewport, nx, ny, &bx, &by);
        lines->push_back({.ax = ax, .ay = ay, .bx = bx, .by = by, .argb = argb, .thickness = thickness});
    };
    for (const ViewCam& c : get_all()) {
        const V3 d = c.pos - camera_pos;
        if (get_dot(d, d) > kMaxFrustumReach * kMaxFrustumReach)
            continue;
        const bool lit = node_create::name_hash(c.name) == selected;
        const uint32_t colour = lit ? 0xFFFFF060 : 0xFFFF9030;
        const float thickness = lit ? 2.0f : 1.25f;
        std::array<V3, 4> corners{};
        frustum_corners({.pos = c.pos, .angles = c.angles}, lens.tan_x_, lens.tan_y_, kFrustumDepth, corners);
        for (int i = 0; i < 4; ++i) {
            line(c.pos, core::at(corners, i), colour, thickness);
            line(core::at(corners, i), core::at(corners, (i + 1) % 4), colour, thickness);
        }
        // (its up: a small triangle over the top edge; what it looks at: a line to the target and a cross there)
        const V3 top = (core::at(corners, 0) + core::at(corners, 1)) * 0.5f;
        float yaw = 0.0f;
        float pitch = 0.0f;
        yaw_pitch_of(c.angles, {.yaw = &yaw, .pitch = &pitch});
        const V3 up = frame_from_yaw_pitch(yaw, pitch).up * 30.0f;
        line(core::at(corners, 0) * kNearWeight + core::at(corners, 1) * kFarWeight, top + up, colour, thickness);
        line(core::at(corners, 1) * kNearWeight + core::at(corners, 0) * kFarWeight, top + up, colour, thickness);
        const V3 target = target_of(c.pos, c.angles, c.distance);
        line(c.pos, target, lit ? kLitColour : kUnlitColour, 1.0f);
        const float s = lit ? 25.0f : 15.0f;
        line(target - V3{.x = s, .y = 0, .z = 0}, target + V3{.x = s, .y = 0, .z = 0}, colour, 1.0f);
        line(target - V3{.x = 0, .y = s, .z = 0}, target + V3{.x = 0, .y = s, .z = 0}, colour, 1.0f);
        line(target - V3{.x = 0, .y = 0, .z = s}, target + V3{.x = 0, .y = 0, .z = s}, colour, 1.0f);
    }
}

void inspector_section(uint32_t node, ImFont* mono, ImFont* small_font)
{
    const std::vector<ViewCam> all = get_all();
    const ViewCam* c = node ? get_find(all, node) : nullptr;
    if (!c)
        return;
    ImGui::Spacing();
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Camera###camera", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    float yaw = 0.0f;
    float pitch = 0.0f;
    yaw_pitch_of(c->angles, {.yaw = &yaw, .pitch = &pitch});
    float pitch_degrees = pitch * kDegreesPerHalfTurn / maths::kPi;
    ImGui::PushFont(mono);
    ImGui::SetNextItemWidth(kFieldWidth * ImGui::GetFontSize() / ui_layout::kReferenceFontSize);
    if (ImGui::DragFloat("Pitch##camera", &pitch_degrees, kPitchDragSpeed, -kMaxPitchDegrees, kMaxPitchDegrees, "%.1f deg",
                         ImGuiSliderFlags_AlwaysClamp)) {
        const float value = pitch_degrees;
        level_editor_cameras_state().queue_.emplace_back([node, value] { set_pitch(checksums::Name{node}, value, false); });
    }
    if (ImGui::IsItemDeactivated())
        level_editor_cameras_state().queue_.emplace_back([] { level_editor_cameras_state().dragging_ = false; });
    float distance = c->distance;
    ImGui::SetNextItemWidth(kFieldWidth * ImGui::GetFontSize() / ui_layout::kReferenceFontSize);
    if (ImGui::DragFloat("Target##camera", &distance, kTargetDragSpeed, kMinDistance, kMaxDistance, "%.0f units",
                         ImGuiSliderFlags_AlwaysClamp)) {
        const float value = distance;
        level_editor_cameras_state().queue_.emplace_back([node, value] { place_at_range(checksums::Name{node}, value, false); });
    }
    if (ImGui::IsItemDeactivated())
        level_editor_cameras_state().queue_.emplace_back([] { level_editor_cameras_state().dragging_ = false; });
    const V3 target = target_of(c->pos, c->angles, c->distance);
    const V3 offset = position_offset(c->pos, c->angles, c->distance);
    ImGui::PushFont(small_font);
    gui::text("targetOffset   (%.1f, %.1f, %.1f)", target.x, target.y, target.z);
    gui::text("positionOffset (%.1f, %.1f, %.1f)", offset.x, offset.y, offset.z);
    ImGui::PopFont();
    ImGui::PopFont();
    if (ImGui::IsItemHovered())
        gui::tooltip("Computed from position and heading");
    if (ImGui::Button("Look through##camera"))
        level_editor_cameras_state().queue_.emplace_back([node] { get_preview(node); });
    ImGui::SameLine();
    if (ImGui::Button("Put at the view##camera"))
        level_editor_cameras_state().queue_.emplace_back([node] { aim_from_view(node); });
    if (ImGui::IsItemHovered())
        gui::tooltip("Move the camera to the current view");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ui_theme::kDangerButtonColour);
    if (ImGui::Button("Delete##camera"))
        level_editor_cameras_state().queue_.emplace_back([node] { drop_out(node); });
    ImGui::PopStyleColor();
}

void list_section(ImFont* mono, ImFont* small_font)
{
    ImGui::Spacing();
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Cameras###cameras"))
        return;
    ImGui::PushFont(small_font);
    gui::text_disabled("Camera nodes, saved with the layout");
    ImGui::PopFont();
    if (ImGui::Button("New Camera from View"))
        level_editor_cameras_state().queue_.emplace_back([] { create_from_view(); });
    const std::vector<ViewCam> all = get_all();
    if (all.empty())
        gui::text_disabled("None yet.");
    ImGui::PushFont(mono);
    for (const ViewCam& c : all) {
        const uint32_t name = node_create::name_hash(c.name);
        ImGui::PushID(static_cast<int>(name));
        ImGui::TextUnformatted(c.name.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("View Through"))
            level_editor_cameras_state().queue_.emplace_back([name] { get_preview(name); });
        ImGui::PopID();
    }
    ImGui::PopFont();
}

} // namespace frontend::level_editor::cameras
