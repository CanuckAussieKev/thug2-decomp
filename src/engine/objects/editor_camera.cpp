// The editor camera component (GameObjects::EditorViewModule) and its ground checks: see editor_camera.h. From the
// disassembly of 0x004E4ED0-0x004E65F2. The x87 roundings are kept: values the original stores are rounded to
// float, the ones it keeps on the FPU stack are worked in double.

#include "engine/objects/editor_camera.h"
#include "game/park_editing/rail_editor.h"
#include "engine/gfx/engine_shadows.h"

#include <memory>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/mat4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/smtptr.h"
#include "engine/script/cache.h"
#include "engine/script/nodes.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "game/park_editing/park_editor.h"
#include "engine/objects/input_component.h"
#include "game/skate/skate_module.h"
#include "engine/sys/game_config.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"

namespace objects {

static uint32_t g_editor_camera_skip_names[4]{0x0B38ED6B, 0xDEA3057B, 0x9FBBD5F0, 0x8F308205};
static uint32_t g_editor_camera_name{0x43BFAB74};
static uint32_t g_editor_camera_seen[20];
static uint8_t g_editor_camera_flagged;
static int g_editor_camera_seen_count;

namespace {

constexpr uint32_t kEditorCamera = 0xAC8946E2;
constexpr uint32_t kInput = 0x27D7CD28;
constexpr float kTwoPi = 6.2831854820251465f;      // 0x0064545C
constexpr float kAbove = 10.0f;                    // 0x00646710
constexpr float kDown = 10000.0f;                  // 0x0064B308
constexpr float kMaxSlope = 0.08699999749660492f;  // 0x0064B304 (the ground normal's y at least this)
constexpr float kWallHeight = 72.0f;               // 0x00646244
constexpr float kNearZoom = 100.0f;                // 0x006463D4
constexpr float kStickScale = 0.0078125f;          // 0x0064A2FC (1 / 128)
constexpr float kDeadZone = 0.009999999776482582f; // 0x0064544C
constexpr double kHeightSlack = 0.01;              // 0x0064B310

// Script globals (Scripts::fetch_float).
float get_global(uint32_t name) { return script::fetch_float(name); }

uint8_t& get_flagged() { return g_editor_camera_flagged; }
int& get_seen_count() { return g_editor_camera_seen_count; }
uint32_t* get_seen() { return g_editor_camera_seen; }

// The collision feeler (render::ProbeRay): its line, a hit callback, the hit's point and normal; what it hit held by
// a smart pointer (let go of by its destructor).
class Feeler {
public:
    Feeler() = default;
    ~Feeler() = default;
    Feeler(const Feeler&) = delete;
    Feeler& operator=(const Feeler&) = delete;
    void set_excluded(uint32_t skip, uint32_t need)
    {
        as_feeler()->set_excluded(static_cast<uint16_t>(skip), static_cast<uint16_t>(need));
    }
    void change_origin(const maths::Vec4& v) { as_feeler()->change_origin(&v); }
    void change_finish(const maths::Vec4& v) { as_feeler()->change_finish(&v); }
    bool get_hit_shape(int a, int b) { return as_feeler()->get_hit_shape(a != 0, b != 0) != 0; }
    render::ProbeRay* as_feeler() { return &ray_; }
    maths::Vec4& kickoff() { return ray_.line_.start; }
    maths::Vec4& get_end() { return ray_.line_.end; }
    render::FeelerCallback& hook_fn() { return ray_.callback_; }
    const maths::Vec4& get_point() const { return ray_.contact_pos_; }
    const maths::Vec4& get_normal() const { return ray_.surface_dir_; }

private:
    render::ProbeRay ray_;
};

// The rail editor's 0x004F3D30 (thiscall; the editor from 0x004F4970), around the cursor's move in the park editor.
void rail_editor_moving(int moved) { park_editing::RailAuthoring::sole()->change_live(moved != 0); }
bool in_park_editor() { return modules::g_skate_module->level_ == 0xE8B4B836; }

} // namespace

// 0x004E4F50
bool script_flagged(uint32_t name)
{
    for (int i = 0; i < get_seen_count(); ++i) {
        if (get_seen()[i] == name)
            return false;
    }
    get_seen()[get_seen_count()++] = name;
    if (name == g_editor_camera_name)
        return false;
    for (uint32_t skip_name : g_editor_camera_skip_names) {
        if (name == skip_name)
            return true;
    }
    script::ScriptStore* cache = script::g_script_store;
    const uint8_t* token = cache->fetch_script(name);
    if (!token)
        return false;
    token = script::skip_to_end_of_statement(token);
    while (*token != 0x24) { // (end of script)
        const uint8_t* next;
        if (*token == 0x16) { // a name: a script called?
            uint32_t called = script::fetch_word32(token + 1);
            next = token + 5;
            bool flagged = false;
            if (*next == 0x42) { // (a name after a colon-like 0x42 token: the second name's the script)
                ++next;
                if (*next == 0x16) {
                    called = script::fetch_word32(next + 1);
                    next += 5;
                    flagged = script_flagged(called);
                } else {
                    token = script::skip_to_end_of_statement(next);
                    continue;
                }
            } else {
                flagged = script_flagged(called);
            }
            if (flagged) {
                cache->release_script(name);
                return true;
            }
            token = script::skip_to_end_of_statement(next);
        } else if (*token == 0x47) {
            token = script::step_over_token(token);
            if (*token == 0x39)
                ++token;
        } else {
            token = script::skip_to_end_of_statement(token);
        }
    }
    cache->release_script(name);
    return false;
}

// 0x004E5320
void flag_scripted_hit(render::ProbeRay* feeler)
{
    if (!feeler->hit_.face_flag40_)
        return;
    uint32_t node_name = feeler->hit_.object_checksum_;
    if (!node_name)
        return;
    script::Struct* node = level_nodes::fetch_vertex(node_name, false);
    if (!node)
        return;
    uint32_t script = 0;
    node->read_hash_value(0x2CA8A299, &script, 0);
    if (!script)
        return;
    get_seen_count() = 0;
    if (script_flagged(script))
        get_flagged() = 1;
}

// 0x004E5390
bool flagged_object_between(const maths::Vec4& start, const maths::Vec4& end)
{
    get_flagged() = 0;
    Feeler feeler;
    feeler.kickoff() = start;
    feeler.get_end() = end;
    feeler.set_excluded(0, 0x50);
    feeler.hook_fn() = &flag_scripted_hit;
    feeler.get_hit_shape(1, 0);
    return get_flagged() != 0;
}

// 0x004E5940
memory::owner<ObjectModule*> EditorViewModule::fabricate() { return new EditorViewModule; }

// 0x004E5060
EditorViewModule::EditorViewModule()
{
    kind_tag_ = kEditorCamera;
    for (maths::Vec4* v : {&focus_, &viewer_, &cursor_, &right_side_, &forward_})
        *v = {0.0f, 0.0f, 0.0f, 1.0f};
    zoom_ = min_zoom_ = max_zoom_ = heading_ = lean_angle_ = min_tilt_ = max_tilt_ = level_y_ = min_height_ = 0.0f;
    shadow_handle_ = nullptr;
    input_ptr_ = nullptr;
    simple_collision_ = false;
    no_wall_check_ = false;
}

// 0x004E5100 (0x004E59A0 the deleting one)
EditorViewModule::~EditorViewModule() { shadow_handle_.reset(); }

// 0x004E4ED0
void EditorViewModule::configure_from_data(script::Struct* params)
{
    min_height_ = 0.0f;
    params->fetch_float(0x9CDF40CD, &min_height_, 0); // min_height
    min_zoom_ = 100.0f;
    params->fetch_float(0x52EECB98, &min_zoom_, 0);
    max_zoom_ = 2000.0f;
    params->fetch_float(0x53E2512C, &max_zoom_, 0);
    simple_collision_ = params->has_bare_flag(0xA0D7FBAB); // SimpleCollision
    no_wall_check_ = params->has_bare_flag(0xF5680DEF);
}

// 0x004E51F0
void EditorViewModule::seal() { input_ptr_ = static_cast<PadInputModule*>(entity_->fetch_module(kInput)); }

// 0x004E5170
void EditorViewModule::build_blob_shadow()
{
    if (shadow_handle_)
        return;
    shadow_handle_ = std::make_unique<render::BlobShadow>();
    shadow_handle_->size_factor_ = 1.2f; // (no null check)
    shadow_handle_->load_mesh("Ped_Shadow");
}

// 0x004E59D0
void EditorViewModule::conceal(bool hide)
{
    if (!hide)
        build_blob_shadow();
    else if (shadow_handle_) {
        shadow_handle_.reset();
    }
}

// 0x004E5A10
void EditorViewModule::resync_ground_shadow()
{
    maths::Vec4 top = {cursor_.x, cursor_.y + level_y_, cursor_.z, cursor_.w};
    Feeler feeler;
    feeler.change_origin(top);
    maths::Vec4 bottom = {top.x, top.y - kDown, top.z, top.w};
    feeler.change_finish(bottom);
    if (!feeler.get_hit_shape(1, 0)) {
        shadow_handle_.reset();
        return;
    }
    build_blob_shadow();
    maths::Vec4 point = feeler.get_point();
    maths::Mat4 identity;
    identity.reset_to_identity();
    maths::Vec4 normal = feeler.get_normal();
    shadow_handle_->tick_frame(&point, &identity, normal);
}

// 0x004E5460
bool EditorViewModule::ground_height(maths::Vec4 start, maths::Vec4 end, float* height)
{
    Feeler feeler;
    feeler.change_origin(start);
    feeler.change_finish(end);
    if (!feeler.get_hit_shape(1, 0))
        return false;
    get_flagged() = 0;
    if (simple_collision_) {
        *height = feeler.get_point().y;
        return true;
    }
    flag_scripted_hit(feeler.as_feeler());
    if (get_flagged())
        return false;
    if (feeler.get_normal().y < kMaxSlope)
        return false;
    *height = feeler.get_point().y;
    maths::Vec4 point = feeler.get_point();
    return !flagged_object_between(start, point);
}

// 0x004E5640
bool EditorViewModule::place_cursor(maths::Vec4 previous, float step)
{
    float above = cursor_.y + step;
    float below = get_global(0xF230D521) + cursor_.y;
    float height;
    if (!ground_height({cursor_.x, above + level_y_, cursor_.z, cursor_.w},
                       {cursor_.x, below + level_y_, cursor_.z, cursor_.w}, &height))
        return false;
    // Nothing in the way at head height from where it was?
    double was = static_cast<double>(previous.y) + level_y_;
    auto top = static_cast<float>((height > was ? static_cast<double>(height) : was) + kWallHeight);
    maths::Vec4 to = {cursor_.x, top, cursor_.z, cursor_.w};
    maths::Vec4 from = {previous.x, top, previous.z, previous.w};
    bool clear = true;
    if (!simple_collision_ && flagged_object_between(from, to))
        clear = false;
    if (!no_wall_check_) {
        Feeler feeler;
        feeler.set_excluded(0x10, 0);
        feeler.change_origin(from);
        feeler.change_finish(to);
        if (feeler.get_hit_shape(1, 0))
            clear = false;
    }
    if (!clear)
        return false;
    if (height >= cursor_.y) { // (not a number goes down)
        // Up: the height above it less the rise (the editor's own camera only if it's no lower than 0x71EC75E9).
        auto lowered = static_cast<float>(level_y_ - (static_cast<double>(height) - cursor_.y));
        if (!simple_collision_ || entity_->ident_ == 0x5B509AD3 || get_global(0x71EC75E9) < level_y_)
            level_y_ = lowered;
        if (level_y_ < min_height_)
            level_y_ = min_height_;
        cursor_.y = height;
        return true;
    }
    // Down: the drop added to the height (unless it's a small one with the camera low).
    double drop = static_cast<double>(cursor_.y) - height;
    if (level_y_ > 1.0f || !(drop <= step))
        level_y_ = static_cast<float>(drop + level_y_);
    cursor_.y = height;
    return true;
}

// 0x004E5210
int EditorViewModule::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* /*script*/)
{
    if (name == 0x69CB3AE1) {
        script::VectorValue position;
        params->fetch_vec(0xB9D31B0A, &position, 1); // Position
        cursor_ = {position.x, position.y, position.z, position.w};
        level_y_ = 0.0f;
        tick_frame();
        level_y_ = position.y - cursor_.y;
        return 1;
    }
    if (name == 0xF362BBBF) {
        params->fetch_vec(0xB9D31B0A, reinterpret_cast<script::VectorValue*>(&cursor_), 0);
        focus_ = cursor_;
        zoom_ = 700.0f;
        params->fetch_float(0xC48391A5, &zoom_, 0);
        lean_angle_ = 0.6f;
        params->fetch_float(0xE3C07609, &lean_angle_, 0); // tilt
        level_y_ = 0.0f;
        params->fetch_float(0xB8D629CA, &level_y_, 0);
        return 1;
    }
    return 2;
}

// 0x004E5C20
void EditorViewModule::tick_frame()
{
    min_tilt_ = get_global(0x7D16D113);
    max_tilt_ = get_global(0x411BEE4A);
    const PadSnapshot& pad = input_ptr_->pad_primary_;

    // Heading, wrapped into 0..2 pi (the comparison on the unrounded sum, as the original).
    double heading = static_cast<double>(get_global(0x8242D878)) * pad.scaled_stick_[0] + heading_;
    heading_ = static_cast<float>(heading);
    if (heading > kTwoPi) {
        double h = heading_;
        do
            h -= kTwoPi;
        while (h > kTwoPi);
        heading_ = static_cast<float>(h);
    }
    if (heading_ < 0.0f) {
        double h = heading_;
        do
            h += kTwoPi;
        while (h < 0.0f);
        heading_ = static_cast<float>(h);
    }
    // Tilt, within the globals' range.
    double tilt = -(static_cast<double>(get_global(0x3CFA3A72)) * pad.scaled_stick_[1]) + lean_angle_;
    lean_angle_ = static_cast<float>(tilt);
    if (tilt > max_tilt_)
        lean_angle_ = max_tilt_;
    if (lean_angle_ < min_tilt_)
        lean_angle_ = min_tilt_;

    // The zoom and height buttons by controller type.
    bool zoom_in = false, zoom_out = false, raise = false, lower = false;
    int controller = game_config::g_platform;
    uint32_t buttons = input_ptr_->events_;
    if (controller == 4 || controller == 7) {
        zoom_in = (buttons & 0x400000) != 0;
        zoom_out = (buttons & 0x800000) != 0;
        if (park_editing::g_park_editor->cur_phase_ == 0x7ECA21E5) {
            raise = (buttons & 0x1000) != 0;
            lower = (buttons & 0x4000) != 0;
        } else {
            raise = (buttons & 0x2000) != 0;
            lower = (buttons & 0x1000) != 0;
        }
    } else if (controller == 5) {
        if (buttons & 0x1000000) {
            zoom_in = (buttons & 0x2000) != 0;
            zoom_out = (buttons & 0x1000) != 0;
        } else {
            lower = (buttons & 0x2000) != 0;
            raise = (buttons & 0x1000) != 0;
        }
    } else {
        zoom_in = pad.shoulder_right_a_.held_down_ != 0;
        zoom_out = pad.shoulder_right_b_.held_down_ != 0;
        raise = pad.shoulder_left_a_.held_down_ != 0;
        lower = pad.shoulder_left_b_.held_down_ != 0;
    }
    double zoom_rate = get_global(0x4E58F829);
    if (!zoom_in)
        zoom_rate = zoom_out ? -zoom_rate : 0.0f;
    double zoom = zoom_rate * zoom_ + zoom_;
    zoom_ = static_cast<float>(zoom);
    if (zoom < min_zoom_)
        zoom_ = min_zoom_;
    if (zoom_ > max_zoom_)
        zoom_ = max_zoom_;
    auto zoom_fraction =
        static_cast<float>((static_cast<double>(zoom_) - min_zoom_) / (static_cast<double>(max_zoom_) - min_zoom_));

    // Raising / lowering the height (by the zoom; slowly when close), not through a ceiling.
    float near_rate = get_global(0x604961F4);
    auto rise =
        static_cast<float>((static_cast<double>(get_global(0x5C445EAD)) - near_rate) * zoom_fraction + near_rate);
    if (zoom_ < kNearZoom)
        rise = 0.2f;
    if (!raise) {
        if (!lower)
            rise = 0.0f;
        else
            rise = -rise;
    }
    if ((raise || lower) && rise > 0.0f) {
        maths::Vec4 from = {cursor_.x, static_cast<float>(static_cast<double>(cursor_.y) + level_y_ + kAbove),
                            cursor_.z, cursor_.w};
        maths::Vec4 to = {cursor_.x, from.y - kDown, cursor_.z, cursor_.w};
        Feeler ceiling;
        ceiling.change_origin(from);
        ceiling.change_finish(to);
        if (ceiling.get_hit_shape(1, 0)) {
            float room = from.y - ceiling.get_point().y;
            if (get_global(0x61D56FA0) < room)
                rise = 0.0f;
            if (room < 0.0f)
                rise = 0.0f;
        } else {
            rise = 0.0f;
        }
    }
    level_y_ = rise + level_y_;

    modules::Skate* skate = modules::g_skate_module;
    if (in_park_editor())
        rail_editor_moving(0);

    // The cursor kept under anything above it, and the height under any ceiling.
    Feeler feeler;
    {
        float height = level_y_;
        maths::Vec4 above = {cursor_.x, static_cast<float>(static_cast<double>(height) + cursor_.y), cursor_.z,
                             static_cast<float>(static_cast<double>(cursor_.w) + 1.0f)};
        feeler.change_origin(above);
        feeler.change_finish(cursor_);
        if (feeler.get_hit_shape(1, 0)) {
            level_y_ = static_cast<float>((static_cast<double>(cursor_.y) + level_y_) - feeler.get_point().y);
            cursor_ = feeler.get_point();
        }
        feeler.change_origin(cursor_);
        float up = level_y_ + kAbove;
        maths::Vec4 top = {cursor_.x, up + cursor_.y, cursor_.z,
                           static_cast<float>(static_cast<double>(cursor_.w) + 1.0f)};
        feeler.change_finish(top);
        if (feeler.get_hit_shape(1, 0))
            level_y_ = static_cast<float>((static_cast<double>(feeler.get_point().y) - cursor_.y) - kAbove);
    }
    if (rise < 0.0f && min_height_ <= (static_cast<double>(level_y_) - rise) + kHeightSlack && level_y_ < min_height_)
        level_y_ = min_height_;
    if (level_y_ < 0.0f)
        level_y_ = 0.0f;

    // The heading's axes.
    float heading_now = heading_;
    right_side_ = {static_cast<float>(std::cos(static_cast<double>(heading_now))), 0.0f,
                   static_cast<float>(-std::sin(static_cast<double>(heading_now))), 1.0f};
    forward_ = {static_cast<float>(std::sin(static_cast<double>(heading_now))), 0.0f,
                static_cast<float>(std::cos(static_cast<double>(heading_now))), 1.0f};
    float near_speed = get_global(0x83BDE786);
    double speed = (static_cast<double>(get_global(0xBFB0D8DF)) - near_speed) * zoom_fraction + near_speed;
    if (zoom_ < kNearZoom)
        speed = 1.0f;

    // The left stick (or the d-pad) moves the cursor along the axes.
    auto side = static_cast<float>(static_cast<double>(pad.stick_[2]) * kStickScale);
    double ahead = static_cast<double>(pad.stick_[3]) * kStickScale;
    if (std::fabs(side) < kDeadZone) {
        if (pad.left_side_.held_down_)
            side = -0.5f;
        if (pad.right_side_.held_down_)
            side = 0.5f;
    }
    if (std::fabs(ahead) < kDeadZone) {
        if (pad.up_dir_.held_down_)
            ahead = -0.5f;
        if (pad.btn_down_.held_down_)
            ahead = 0.5f;
    }
    auto a1 = static_cast<float>(ahead * forward_.w);
    auto a2 = static_cast<float>(ahead * forward_.z);
    auto a3 = static_cast<float>(ahead * forward_.y);
    double a4 = ahead * forward_.x;
    auto b1 = static_cast<float>(a1 * speed), b2 = static_cast<float>(a2 * speed), b3 = static_cast<float>(a3 * speed);
    auto b4 = static_cast<float>(a4 * speed);
    double c1 = static_cast<double>(side) * right_side_.w, c2 = static_cast<double>(side) * right_side_.z;
    float c3 = side * right_side_.y, c4 = side * right_side_.x;
    auto d1 = static_cast<float>(c1 * speed), d2 = static_cast<float>(c2 * speed), d3 = static_cast<float>(c3 * speed);
    double x0 = speed * c4;
    maths::Vec4 previous = cursor_;
    cursor_.x = static_cast<float>((x0 + b4) + cursor_.x);
    cursor_.y = static_cast<float>((static_cast<double>(d3) + b3) + cursor_.y);
    cursor_.z = static_cast<float>((static_cast<double>(d2) + b2) + cursor_.z);
    cursor_.w = static_cast<float>((static_cast<double>(d1) + b1) + cursor_.w);
    if (!place_cursor(previous, get_global(0xC7210318)))
        cursor_ = previous;
    if (skate->level_ == 0xE8B4B836)
        rail_editor_moving(1);

    // The camera: the focus follows the cursor (its height eased), the camera at the zoom along the tilt.
    focus_.x = cursor_.x;
    focus_.z = cursor_.z;
    float ease = get_global(0x725AD02C);
    focus_.y = static_cast<float>(((static_cast<double>(cursor_.y) + level_y_) - focus_.y) * ease + focus_.y);
    float tilt_now = lean_angle_;
    auto at_x = static_cast<float>(std::cos(static_cast<double>(tilt_now)) * forward_.x);
    auto at_z = static_cast<float>(std::cos(static_cast<double>(tilt_now)) * forward_.z);
    auto at_y = static_cast<float>(std::sin(static_cast<double>(tilt_now)));
    maths::Mat4 look;
    look.row[0] = {right_side_.x, right_side_.y, right_side_.z, 0.0f};
    look.row[1] = {
        static_cast<float>(static_cast<double>(at_y) * right_side_.z - static_cast<double>(at_z) * right_side_.y),
        static_cast<float>(static_cast<double>(at_z) * right_side_.x - static_cast<double>(at_x) * right_side_.z),
        static_cast<float>(static_cast<double>(at_x) * right_side_.y - static_cast<double>(at_y) * right_side_.x),
        0.0f};
    look.row[2] = {at_x, at_y, at_z, 0.0f};
    look.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    entity_->basis_frame_ = look;
    float zoom_now = zoom_;
    float off_w = previous.w * zoom_now; // (the cursor's w from before the move, as the original reuses it)
    float off_z = at_z * zoom_now, nudge_along_y = at_y * zoom_now, side_shift_x = at_x * zoom_now;
    viewer_.x = side_shift_x + focus_.x;
    viewer_.y = focus_.y + nudge_along_y;
    viewer_.z = focus_.z + off_z;
    viewer_.w = focus_.w + off_w;
    viewer_.w = 1.0f;
    entity_->at_point_ = viewer_;
    resync_ground_shadow();
}

} // namespace objects
