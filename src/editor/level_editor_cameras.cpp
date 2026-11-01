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
#include <map>
#include <memory>
#include <vector>

#include "editor/level_editor_fields.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_nodes.h"
#include "engine/maths/vec4.h"
#include "engine/objects/movie_cfuncs.h"
#include "engine/script/node_manager.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "platform/run_log.h"
#include "imgui.h"

namespace frontend::level_editor::cameras {

// --- the pure parts -------------------------------------------------------------------------------------------------

namespace {

constexpr float kPi = 3.14159265f;

// Numbers separated by commas, the whole text (as the layout's other records read them).
bool read_numbers(const std::string& text, float* out, int count)
{
    const char* at = text.c_str();
    for (int i = 0; i < count; ++i) {
        char* end = nullptr;
        out[i] = std::strtof(at, &end);
        if (end == at || !std::isfinite(out[i]))
            return false;
        at = end;
        if (i + 1 < count) {
            if (*at != ',')
                return false;
            ++at;
        }
    }
    return *at == '\0';
}

bool read_triple(const std::string& text, V3* out)
{
    float v[3];
    if (!read_numbers(text, v, 3))
        return false;
    *out = {v[0], v[1], v[2]};
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
V3 angles_for(float yaw, float pitch) { return {-pitch, yaw, 0.0f}; }

void yaw_pitch_of(V3 angles, float* yaw, float* pitch)
{
    *yaw = angles.y;
    *pitch = -angles.x;
}

V3 get_forward(V3 angles)
{
    // (RotationZ last: it turns that row about the world's z axis - the editor's cameras keep z 0)
    const float cx = std::cos(angles.x);
    const float sx = std::sin(angles.x);
    const V3 v = {std::sin(angles.y) * cx, -sx, std::cos(angles.y) * cx};
    const float cz = std::cos(angles.z);
    const float sz = std::sin(angles.z);
    return {v.x * cz - v.y * sz, v.x * sz + v.y * cz, v.z};
}

V3 target_of(V3 pos, V3 angles, float distance) { return pos + get_forward(angles) * clamp_distance(distance); }

V3 position_offset(V3 pos, V3 angles, float distance) { return pos - target_of(pos, angles, distance); }

int shot_frames(float seconds)
{
    if (!std::isfinite(seconds))
        seconds = kShotDefaultSeconds;
    seconds = std::max(kShotMinSeconds, std::min(kShotMaxSeconds, seconds));
    return static_cast<int>(std::lround(seconds * 60.0f));
}

void frustum_corners(V3 pos, V3 angles, float tan_x, float tan_y, float depth, V3 out[4])
{
    float yaw = 0.0f, pitch = 0.0f;
    yaw_pitch_of(angles, &yaw, &pitch);
    const TickCycle f = frame_from_yaw_pitch(yaw, pitch);
    const V3 centre = pos - f.back * depth;
    const V3 right = f.right * (tan_x * depth);
    const V3 up = f.up * (tan_y * depth);
    out[0] = centre + right + up;
    out[1] = centre - right + up;
    out[2] = centre - right - up;
    out[3] = centre + right - up;
}

bool get_same(const ViewCam& a, const ViewCam& b)
{
    auto same = [](V3 p, V3 q) { return p.x == q.x && p.y == q.y && p.z == q.z; };
    return a.name == b.name && same(a.pos, b.pos) && same(a.angles, b.angles) && a.distance == b.distance;
}

std::string write_record(const ViewCam& c)
{
    char text[320];
    std::snprintf(text, sizeof(text), "camera %s pos=%.9g,%.9g,%.9g angles=%.9g,%.9g,%.9g dist=%.9g", c.name.c_str(),
                  c.pos.x, c.pos.y, c.pos.z, c.angles.x, c.angles.y, c.angles.z, c.distance);
    return text;
}

bool read_record(std::istringstream& in, ViewCam* out)
{
    ViewCam c;
    if (!(in >> c.name) || !node_create::valid_name(c.name))
        return false;
    bool have_pos = false, have_angles = false, have_distance = false;
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
            if (have_distance || !read_numbers(value, &c.distance, 1))
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
std::vector<std::function<void()>> g_queue;
// A field being dragged (pitch / distance): the cameras before its first change (one undo step).
bool g_dragging;

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
        node_create::Prop f = {node_create::Prop::kVector, key};
        f.value[0] = v.x;
        f.value[1] = v.y;
        f.value[2] = v.z;
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
    const Key keys[2] = {{fields::hash_of_name("targetOffset"), target_of(c.pos, c.angles, c.distance)},
                         {fields::hash_of_name("positionOffset"), position_offset(c.pos, c.angles, c.distance)}};
    for (const Key& k : keys) {
        const fields::Own own = fields::own_in(node, k.key, false);
        const bool same = own.present && own.value.kind == fields::Kind::kVec4 && own.value.numbers[0] == k.value.x &&
                          own.value.numbers[1] == k.value.y && own.value.numbers[2] == k.value.z;
        if (same)
            continue;
        fields::Own put = own;
        put.present = true;
        put.value = fields::Value{};
        put.value.kind = fields::Kind::kVec4;
        put.value.numbers[0] = k.value.x;
        put.value.numbers[1] = k.value.y;
        put.value.numbers[2] = k.value.z;
        fields::put_in(node, k.key, false, put);
    }
}

bool look_through(const ViewCam& c)
{
    float yaw = 0.0f, pitch = 0.0f;
    yaw_pitch_of(c.angles, &yaw, &pitch);
    look_from(c.pos, yaw, pitch);
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
double g_shot_until;
double get_now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

using Function = bool (*)(script::Struct*, script::Script*);

// The offsets of the camera `name` (false: no such node).
bool shot_offsets(uint32_t name, V3* target, V3* position)
{
    const std::vector<ViewCam> all = get_all();
    if (const ViewCam* c = get_find(all, name)) {
        *target = target_of(c->pos, c->angles, c->distance);
        *position = position_offset(c->pos, c->angles, c->distance);
        return true;
    }
    script::Struct* node = level_nodes::node_manager_instance() ? level_nodes::fetch_vertex(name, false) : nullptr;
    if (!node)
        return false;
    script::VectorValue t{}, p{};
    if (node->fetch_vec(get_checksums("targetOffset"), &t, 0) &&
        node->fetch_vec(get_checksums("positionOffset"), &p, 0)) {
        *target = {t.x, t.y, t.z};
        *position = {p.x, p.y, p.z};
        return true;
    }
    maths::Vec4 at;
    if (!level_nodes::fetch_place(node, &at))
        return false;
    script::VectorValue a = {};
    node->fetch_vec(get_checksums("Angles"), &a, 0);
    const V3 pos = {at.x, at.y, at.z};
    const V3 angles = {a.x, a.y, a.z};
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
    V3 target{}, position{};
    if (!shot_offsets(name, &target, &position)) {
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
    if (now < g_shot_until) {
        // (the last shot still playing: killed first, as the goals kill theirs)
        auto* kill = new (script::spawn_param_bag()) script::Struct;
        kill->put_name_key(get_checksums("name"), shot);
        commands::cfunc_kill_skater_cam_anim(kill, script);
        kill->get_clear();
        script::delete_struct(kill);
    }
    // The game's own virtual_cam, as view_goals_menu.qb's play_single_view_goals_cam plays a goal's camera.
    auto* anim = new (script::spawn_param_bag()) script::Struct;
    anim->put_name_key(get_checksums("name"), shot);
    anim->put_name_key(0u, get_checksums("virtual_cam"));
    anim->put_name_key(get_checksums("targetid"), get_checksums("world"));
    anim->put_vec3(get_checksums("targetOffset"), target.x, target.y, target.z);
    anim->put_vec3(get_checksums("positionOffset"), position.x, position.y, position.z);
    const int frames = shot_frames(seconds);
    anim->put_int(get_checksums("frames"), frames);
    anim->put_int(get_checksums("skippable"), 1);
    commands::cfunc_play_skater_cam_anim(anim, script);
    anim->get_clear();
    script::delete_struct(anim);
    g_shot_until = now + frames / 60.0;
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
        hud::get_message(message.empty() ? "Couldn't create the camera" : message, 4.0);
        return "";
    }
    hud::get_message("Added camera " + c.name);
    get_log("level editor: camera %s made at (%.1f, %.1f, %.1f), heading %.1f, pitch %.1f", c.name.c_str(), c.pos.x,
            c.pos.y, c.pos.z, view.yaw * 180.0f / kPi, view.pitch * 180.0f / kPi);
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

bool set_pitch(uint32_t name, float degrees, bool done)
{
    const std::vector<ViewCam> all = get_all();
    const ViewCam* c = get_find(all, name);
    if (!c)
        return false;
    if (!g_dragging)
        push_undo();
    g_dragging = !done;
    node_edit::Values values;
    values.pos = c->pos;
    values.angles = c->angles;
    values.angles.x = -std::clamp(degrees, -89.0f, 89.0f) * kPi / 180.0f;
    node_edit::assign_value(name, values);
    return true;
}

bool place_at_range(uint32_t name, float distance, bool done)
{
    const std::vector<ViewCam> all = get_all();
    const ViewCam* c = get_find(all, name);
    if (!c)
        return false;
    if (!g_dragging)
        push_undo();
    g_dragging = !done;
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
    g_queue.clear();
    g_dragging = false;
}

void tick_frame()
{
    std::vector<std::function<void()>> queue;
    queue.swap(g_queue);
    for (auto& action : queue)
        action();
    for (const ViewCam& c : get_all())
        keep_offsets(c);
}

void add_overlay(V3 camera_pos, const TickCycle& frame, const Lens& lens, const uint32_t viewport[4], float near_z,
                 uint32_t selected, std::vector<ui::Seg>* lines)
{
    auto line = [&](V3 a, V3 b, uint32_t argb, float thickness) {
        if (!clip_to_near(camera_pos, frame, near_z + 0.01f, &a, &b))
            return;
        float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f, nx = 0.0f, ny = 0.0f;
        if (!cast_onto(camera_pos, frame, lens, a, near_z, &nx, &ny))
            return;
        ndc_to_pixel(viewport, nx, ny, &ax, &ay);
        if (!cast_onto(camera_pos, frame, lens, b, near_z, &nx, &ny))
            return;
        ndc_to_pixel(viewport, nx, ny, &bx, &by);
        lines->push_back({ax, ay, bx, by, argb, thickness});
    };
    for (const ViewCam& c : get_all()) {
        const V3 d = c.pos - camera_pos;
        if (get_dot(d, d) > 12000.0f * 12000.0f)
            continue;
        const bool lit = node_create::name_hash(c.name) == selected;
        const uint32_t colour = lit ? 0xFFFFF060 : 0xFFFF9030;
        const float thickness = lit ? 2.0f : 1.25f;
        V3 corners[4];
        frustum_corners(c.pos, c.angles, lens.tan_x_, lens.tan_y_, 120.0f, corners);
        for (int i = 0; i < 4; ++i) {
            line(c.pos, corners[i], colour, thickness);
            line(corners[i], corners[(i + 1) % 4], colour, thickness);
        }
        // (its up: a small triangle over the top edge; what it looks at: a line to the target and a cross there)
        const V3 top = (corners[0] + corners[1]) * 0.5f;
        float yaw = 0.0f, pitch = 0.0f;
        yaw_pitch_of(c.angles, &yaw, &pitch);
        const V3 up = frame_from_yaw_pitch(yaw, pitch).up * 30.0f;
        line(corners[0] * 0.75f + corners[1] * 0.25f, top + up, colour, thickness);
        line(corners[1] * 0.75f + corners[0] * 0.25f, top + up, colour, thickness);
        const V3 target = target_of(c.pos, c.angles, c.distance);
        line(c.pos, target, lit ? 0xC0FFF060 : 0x60FF9030, 1.0f);
        const float s = lit ? 25.0f : 15.0f;
        line(target - V3{s, 0, 0}, target + V3{s, 0, 0}, colour, 1.0f);
        line(target - V3{0, s, 0}, target + V3{0, s, 0}, colour, 1.0f);
        line(target - V3{0, 0, s}, target + V3{0, 0, s}, colour, 1.0f);
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
    float yaw = 0.0f, pitch = 0.0f;
    yaw_pitch_of(c->angles, &yaw, &pitch);
    float pitch_degrees = pitch * 180.0f / kPi;
    ImGui::PushFont(mono);
    ImGui::SetNextItemWidth(160.0f * ImGui::GetFontSize() / 13.0f);
    if (ImGui::DragFloat("Pitch##camera", &pitch_degrees, 0.25f, -89.0f, 89.0f, "%.1f deg",
                         ImGuiSliderFlags_AlwaysClamp)) {
        const float value = pitch_degrees;
        g_queue.emplace_back([node, value] { set_pitch(node, value, false); });
    }
    if (ImGui::IsItemDeactivated())
        g_queue.emplace_back([] { g_dragging = false; });
    float distance = c->distance;
    ImGui::SetNextItemWidth(160.0f * ImGui::GetFontSize() / 13.0f);
    if (ImGui::DragFloat("Target##camera", &distance, 5.0f, kMinDistance, kMaxDistance, "%.0f units",
                         ImGuiSliderFlags_AlwaysClamp)) {
        const float value = distance;
        g_queue.emplace_back([node, value] { place_at_range(node, value, false); });
    }
    if (ImGui::IsItemDeactivated())
        g_queue.emplace_back([] { g_dragging = false; });
    const V3 target = target_of(c->pos, c->angles, c->distance);
    const V3 offset = position_offset(c->pos, c->angles, c->distance);
    ImGui::PushFont(small_font);
    ImGui::Text("targetOffset   (%.1f, %.1f, %.1f)", target.x, target.y, target.z);
    ImGui::Text("positionOffset (%.1f, %.1f, %.1f)", offset.x, offset.y, offset.z);
    ImGui::PopFont();
    ImGui::PopFont();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Computed from position and heading");
    if (ImGui::Button("Look through##camera"))
        g_queue.emplace_back([node] { get_preview(node); });
    ImGui::SameLine();
    if (ImGui::Button("Put at the view##camera"))
        g_queue.emplace_back([node] { aim_from_view(node); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Move the camera to the current view");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.22f, 0.30f, 0.85f));
    if (ImGui::Button("Delete##camera"))
        g_queue.emplace_back([node] { drop_out(node); });
    ImGui::PopStyleColor();
}

void list_section(ImFont* mono, ImFont* small_font)
{
    ImGui::Spacing();
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Cameras###cameras"))
        return;
    ImGui::PushFont(small_font);
    ImGui::TextDisabled("Camera nodes, saved with the layout");
    ImGui::PopFont();
    if (ImGui::Button("New Camera from View"))
        g_queue.emplace_back([] { create_from_view(); });
    const std::vector<ViewCam> all = get_all();
    if (all.empty())
        ImGui::TextDisabled("None yet.");
    ImGui::PushFont(mono);
    for (const ViewCam& c : all) {
        const uint32_t name = node_create::name_hash(c.name);
        ImGui::PushID(static_cast<int>(name));
        ImGui::TextUnformatted(c.name.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("View Through"))
            g_queue.emplace_back([name] { get_preview(name); });
        ImGui::PopID();
    }
    ImGui::PopFont();
}

} // namespace frontend::level_editor::cameras
