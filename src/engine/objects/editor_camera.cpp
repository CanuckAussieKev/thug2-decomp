// The editor camera component (GameObjects::EditorViewModule) and its ground checks: see editor_camera.h. From the
// disassembly of 0x004E4ED0-0x004E65F2. The x87 roundings are kept: values the original stores are rounded to
// float, the ones it keeps on the FPU stack are worked in double.

#include "engine/objects/editor_camera.h"
#include "game/park_editing/rail_editor.h"
#include "engine/gfx/engine_shadows.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <gsl/pointers>
#include <array>

#include "engine/maths/mat4.h"
#include "engine/objects/control_pad.h"
#include "engine/render/probe_ray.h"
#include "engine/script/cache.h"
#include "engine/script/nodes.h"
#include "engine/script/qb.h"
#include "engine/script/tokens.h"
#include "engine/script/param_struct.h"
#include "game/park_editing/park_editor.h"
#include "engine/objects/input_component.h"
#include "game/skate/skate_module.h"
#include "engine/sys/game_config.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_obj.h"
#include "engine/maths/trig.h"

namespace objects {

namespace {
constexpr std::size_t kSeenCapacity = 20;
constexpr float kMaxZoom = 2000.0F;
constexpr float kShadowSizeFactor = 1.2F;
constexpr float kStartZoom = 700.0F;
constexpr float kStartLean = 0.6F;
constexpr int kControllerPs2Pad = 7;
constexpr int kControllerXbox = 5;
constexpr uint32_t kButtonZoomIn = 0x400000;
constexpr uint32_t kButtonZoomOut = 0x800000;
constexpr uint32_t kButtonRaiseA = 0x1000;
constexpr uint32_t kButtonLowerA = 0x4000;
constexpr uint32_t kButtonRaiseB = 0x2000;
constexpr uint32_t kButtonLowerB = 0x1000;
constexpr uint32_t kButtonShift = 0x1000000;
constexpr float kRiseStep = 0.2F;
constexpr float kMoveStep = 0.5F;
constexpr uint32_t kSK3KillSkaterFinish = 0x0B38ED6B; // SK3_KillSkater_Finish
constexpr uint32_t kSK3KillSkater = 0xDEA3057B; // SK3_KillSkater
constexpr uint32_t kTeleportSkaterToNode = 0x9FBBD5F0; // TeleportSkaterToNode
constexpr uint32_t kCAGBoundary = 0x8F308205; // CAG_Boundary
constexpr uint32_t kLoadSk5Ed = 0xE8B4B836; // Load_Sk5Ed
constexpr uint32_t kTriggerScript = 0x2CA8A299; // TriggerScript
constexpr uint32_t kMinHeight = 0x9CDF40CD; // min_height
constexpr uint32_t kMinRadius = 0x52EECB98; // min_radius
constexpr uint32_t kMaxRadius = 0x53E2512C; // max_radius
constexpr uint32_t kSimpleCollision = 0xA0D7FBAB; // SimpleCollision
constexpr uint32_t kNoWallCheck = 0xF5680DEF; // text unknown (the flag that turns the wall check off)
constexpr uint32_t kRailEditor = 0x5B509AD3; // RailEditor
constexpr uint32_t kGoalEditorObjectHeight = 0x71EC75E9; // GoalEditor_ObjectHeight
constexpr uint32_t kEditorCamSetCursorPos = 0x69CB3AE1; // EditorCam_SetCursorPos
constexpr uint32_t kPosition = 0xB9D31B0A; // Position
constexpr uint32_t kEditorCamInitialise = 0xF362BBBF; // EditorCam_Initialise
constexpr uint32_t kRadius = 0xC48391A5; // Radius
constexpr uint32_t kTilt = 0xE3C07609; // Tilt
constexpr uint32_t kCursorHeight = 0xB8D629CA; // cursor_height
constexpr uint32_t kEditorCamTiltMin = 0x7D16D113; // EditorCam_TiltMin
constexpr uint32_t kEditorCamTiltMax = 0x411BEE4A; // EditorCam_TiltMax
constexpr uint32_t kEdit = 0x7ECA21E5; // Edit
constexpr uint32_t kEditorCamInOutSpeed = 0x4E58F829; // EditorCam_InOutSpeed
constexpr uint32_t kEditorCamUpDownSpeedMax = 0x5C445EAD; // EditorCam_UpDownSpeedMax
constexpr uint32_t kEditorCamMaxHeight = 0x61D56FA0; // EditorCam_MaxHeight
constexpr uint32_t kEditorCamMoveSpeedMax = 0xBFB0D8DF; // EditorCam_MoveSpeedMax
constexpr uint32_t kEditorCamCursorCollisionFirstUpOffset = 0xC7210318; // EditorCam_CursorCollisionFirstUpOffset
std::array<uint32_t, 4>& editor_camera_skip_names()
{
    static std::array<uint32_t, 4> editor_camera_skip_names{{kSK3KillSkaterFinish, kSK3KillSkater, kTeleportSkaterToNode, kCAGBoundary}};
    return editor_camera_skip_names;
}
constexpr uint32_t kEditorCameraName{0x43BFAB74};
// NOT ORIGINAL grouping - editor_camera.cpp's state (the original's globals). Made on first use, never destroyed
// (editor_camera_state()).
struct EditorCameraState {
    std::array<uint32_t, kSeenCapacity> editor_camera_seen_{};
    uint8_t editor_camera_flagged_{};
    int editor_camera_seen_count_{};
};

EditorCameraState& editor_camera_state()
{
    static core::NoDestroy<EditorCameraState> state;
    return state.get();
}

constexpr uint32_t kEditorCamera = 0xAC8946E2;
constexpr uint32_t kInput = 0x27D7CD28;
constexpr float kAbove = 10.0F;                    // 0x00646710
constexpr float kDown = 10000.0F;                  // 0x0064B308
constexpr float kMaxSlope = 0.08699999749660492F;  // 0x0064B304 (the ground normal's y at least this)
constexpr float kWallHeight = 72.0F;               // 0x00646244
constexpr float kNearZoom = 100.0F;                // 0x006463D4
constexpr float kStickScale = 0.0078125F;          // 0x0064A2FC (1 / 128)
constexpr float kDeadZone = 0.009999999776482582F; // 0x0064544C
constexpr double kHeightSlack = 0.01;              // 0x0064B310

// Script globals (Scripts::fetch_float).
float get_global(uint32_t name) { return script::fetch_float(name); }

uint8_t& get_flagged() { return editor_camera_state().editor_camera_flagged_; }
int& get_seen_count() { return editor_camera_state().editor_camera_seen_count_; }
std::array<uint32_t, kSeenCapacity>& get_seen() { return editor_camera_state().editor_camera_seen_; }

// The collision feeler (render::ProbeRay): its line, a hit callback, the hit's point and normal; what it hit held by
// a smart pointer (let go of by its destructor).
class Feeler {
public:
    Feeler() = default;
    ~Feeler() = default;
    Feeler(const Feeler&) = delete;
    Feeler& operator=(const Feeler&) = delete;
    void set_excluded(render::FaceFlag skip, render::FaceFlag need)
    {
        as_feeler()->set_excluded({.ignore = skip, .require = need});
    }
    void change_origin(const maths::Vec4& v) { as_feeler()->change_origin(&v); }
    void change_finish(const maths::Vec4& v) { as_feeler()->change_finish(&v); }
    bool get_hit_shape(int a, int b) { return as_feeler()->get_hit_shape(a != 0, b != 0) != 0; }
    render::ProbeRay* as_feeler() { return &ray_; }
    maths::Vec4& kickoff() { return ray_.line().start; }
    maths::Vec4& get_end() { return ray_.line().end; }
    render::FeelerCallback& hook_fn() { return ray_.callback(); }
    [[nodiscard]] const maths::Vec4& get_point() const { return ray_.contact_pos(); }
    [[nodiscard]] const maths::Vec4& get_normal() const { return ray_.surface_dir(); }

private:
    render::ProbeRay ray_;
};

// The rail editor's 0x004F3D30 (thiscall; the editor from 0x004F4970), around the cursor's move in the park editor.
void rail_editor_moving(int moved) { park_editing::RailAuthoring::sole()->change_live(moved != 0); }
bool in_park_editor() { return modules::Skate::sole()->level() == kLoadSk5Ed; }

} // namespace

// 0x004E4F50
bool script_flagged(uint32_t name)
{
    for (int i = 0; i < get_seen_count(); ++i) {
        if (core::at(get_seen(), i) == name) {
            return false;
        }
    }
    core::at(get_seen(), get_seen_count()++) = name;
    if (name == kEditorCameraName) {
        return false;
    }
    for (const uint32_t skip_name : editor_camera_skip_names()) {
        if (name == skip_name) {
            return true;
        }
    }
    script::ScriptStore* cache = script::script_store();
    const uint8_t* code = cache->fetch_script(name);
    if (!code) {
        return false;
    }
    script::Tokens token = script::tokens_at(code);
    token = script::skip_to_end_of_statement(token);
    while (script::as_token(*token) != script::QbToken::kScriptEnd) { // (end of script)
        script::Tokens next;
        if (script::as_token(*token) == script::QbToken::kName) { // a name: a script called?
            uint32_t called = script::fetch_word32(token + 1);
            next = token + script::kTokenWithWordBytes;
            bool flagged = false;
            if (script::as_token(*next) == script::QbToken::kColon) { // (a name after a colon-like 0x42 token: the second name's the script)
                ++next;
                if (script::as_token(*next) == script::QbToken::kName) {
                    called = script::fetch_word32(next + 1);
                    next += script::kTokenWithWordBytes;
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
        } else if (script::as_token(*token) == script::QbToken::kShortIf) {
            token = script::step_over_token(token);
            if (script::as_token(*token) == script::QbToken::kNot) {
                ++token;
            }
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
    if (!feeler->hit().face_flag40_) {
        return;
    }
    const uint32_t node_name = feeler->hit().object_checksum_;
    if (!node_name) {
        return;
    }
    script::Struct* node = level_nodes::fetch_vertex(node_name, false);
    if (!node) {
        return;
    }
    uint32_t script = 0;
    node->read_hash_value(kTriggerScript, &script, 0);
    if (!script) {
        return;
    }
    get_seen_count() = 0;
    if (script_flagged(script)) {
        get_flagged() = 1;
    }
}

// 0x004E5390
bool flagged_object_between(const maths::Vec4& start, const maths::Vec4& end)
{
    get_flagged() = 0;
    Feeler feeler;
    feeler.kickoff() = start;
    feeler.get_end() = end;
    feeler.set_excluded(render::kNoFaceFlags, render::kTriggerVolumeFaceFlags);
    feeler.hook_fn() = &flag_scripted_hit;
    feeler.get_hit_shape(1, 0);
    return get_flagged() != 0;
}

// 0x004E5940
gsl::owner<ObjectModule*> EditorViewModule::fabricate() { return new EditorViewModule; }

// 0x004E5060
EditorViewModule::EditorViewModule() : shadow_handle_(nullptr)
{
    set_kind_tag(kEditorCamera);
    for (maths::Vec4* v : {&focus_, &viewer_, &cursor_, &right_side_, &forward_}) {
        *v = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    }
    zoom_ = min_zoom_ = max_zoom_ = heading_ = lean_angle_ = min_tilt_ = max_tilt_ = level_y_ = min_height_ = 0.0F;
}

// 0x004E5100 (0x004E59A0 the deleting one)
EditorViewModule::~EditorViewModule() { shadow_handle_.reset(); }

// 0x004E4ED0
void EditorViewModule::configure_from_data(script::Struct* params)
{
    min_height_ = 0.0F;
    params->fetch_float(kMinHeight, &min_height_, 0); // min_height
    min_zoom_ = 100.0F;
    params->fetch_float(kMinRadius, &min_zoom_, 0);
    max_zoom_ = kMaxZoom;
    params->fetch_float(kMaxRadius, &max_zoom_, 0);
    simple_collision_ = params->has_bare_flag(kSimpleCollision); // SimpleCollision
    no_wall_check_ = params->has_bare_flag(kNoWallCheck);
}

// 0x004E51F0
void EditorViewModule::seal() { input_ptr_ = core::down_cast<PadInputModule>(entity()->fetch_module(kInput)); }

// 0x004E5170
void EditorViewModule::build_blob_shadow()
{
    if (shadow_handle_) {
        return;
    }
    shadow_handle_ = std::make_unique<render::BlobShadow>();
    shadow_handle_->set_size_factor(kShadowSizeFactor); // (no null check)
    shadow_handle_->load_mesh("Ped_Shadow");
}

// 0x004E59D0
void EditorViewModule::conceal(bool hide)
{
    if (!hide) {
        build_blob_shadow();
    } else if (shadow_handle_) {
        shadow_handle_.reset();
    }
}

// 0x004E5A10
void EditorViewModule::resync_ground_shadow()
{
    const maths::Vec4 top = {.x = cursor_.x, .y = cursor_.y + level_y_, .z = cursor_.z, .w = cursor_.w};
    Feeler feeler;
    feeler.change_origin(top);
    const maths::Vec4 bottom = {.x = top.x, .y = top.y - kDown, .z = top.z, .w = top.w};
    feeler.change_finish(bottom);
    if (!feeler.get_hit_shape(1, 0)) {
        shadow_handle_.reset();
        return;
    }
    build_blob_shadow();
    const maths::Vec4 point = feeler.get_point();
    maths::Mat4 identity;
    identity.reset_to_identity();
    const maths::Vec4 normal = feeler.get_normal();
    shadow_handle_->tick_frame(&point, &identity, normal);
}

// 0x004E5460
bool EditorViewModule::ground_height(maths::Vec4 start, maths::Vec4 end, float* height) const
{
    Feeler feeler;
    feeler.change_origin(start);
    feeler.change_finish(end);
    if (!feeler.get_hit_shape(1, 0)) {
        return false;
    }
    get_flagged() = 0;
    if (simple_collision_) {
        *height = feeler.get_point().y;
        return true;
    }
    flag_scripted_hit(feeler.as_feeler());
    if (get_flagged()) {
        return false;
    }
    if (feeler.get_normal().y < kMaxSlope) {
        return false;
    }
    *height = feeler.get_point().y;
    const maths::Vec4 point = feeler.get_point();
    return !flagged_object_between(start, point);
}

// 0x004E5640
bool EditorViewModule::place_cursor(maths::Vec4 previous, float step)
{
    const float above = cursor_.y + step;
    const float below = get_global(0xF230D521) + cursor_.y;
    float height = 0.0F;
    if (!ground_height({.x = cursor_.x, .y = above + level_y_, .z = cursor_.z, .w = cursor_.w},
                       {.x = cursor_.x, .y = below + level_y_, .z = cursor_.z, .w = cursor_.w}, &height)) {
        return false;
    }
    // Nothing in the way at head height from where it was?
    const double was = static_cast<double>(previous.y) + level_y_;
    auto top = static_cast<float>((height > was ? static_cast<double>(height) : was) + kWallHeight);
    const maths::Vec4 to = {.x = cursor_.x, .y = top, .z = cursor_.z, .w = cursor_.w};
    const maths::Vec4 from = {.x = previous.x, .y = top, .z = previous.z, .w = previous.w};
    bool clear = true;
    if (!simple_collision_ && flagged_object_between(from, to)) {
        clear = false;
    }
    if (!no_wall_check_) {
        Feeler feeler;
        feeler.set_excluded(render::kDefaultIgnoredFaceFlags, render::kNoFaceFlags);
        feeler.change_origin(from);
        feeler.change_finish(to);
        if (feeler.get_hit_shape(1, 0)) {
            clear = false;
        }
    }
    if (!clear) {
        return false;
    }
    if (height >= cursor_.y) { // (not a number goes down)
        // Up: the height above it less the rise (the editor's own camera only if it's no lower than 0x71EC75E9).
        auto lowered = static_cast<float>(level_y_ - (static_cast<double>(height) - cursor_.y));
        if (!simple_collision_ || entity()->ident() == kRailEditor || get_global(kGoalEditorObjectHeight) < level_y_) {
            level_y_ = lowered;
        }
        level_y_ = std::max(level_y_, min_height_);
        cursor_.y = height;
        return true;
    }
    // Down: the drop added to the height (unless it's a small one with the camera low).
    const double drop = static_cast<double>(cursor_.y) - height;
    if (level_y_ > 1.0F || !(drop <= step)) {
        level_y_ = static_cast<float>(drop + level_y_);
    }
    cursor_.y = height;
    return true;
}

// 0x004E5210
int EditorViewModule::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* /*script*/)
{
    if (name == kEditorCamSetCursorPos) {
        script::VectorValue position{};
        params->fetch_vec(kPosition, &position, 1); // Position
        cursor_ = {.x = position.x, .y = position.y, .z = position.z, .w = position.w};
        level_y_ = 0.0F;
        tick_frame();
        level_y_ = position.y - cursor_.y;
        return 1;
    }
    if (name == kEditorCamInitialise) {
        params->fetch_vec(kPosition, &cursor_, 0);
        focus_ = cursor_;
        zoom_ = kStartZoom;
        params->fetch_float(kRadius, &zoom_, 0);
        lean_angle_ = kStartLean;
        params->fetch_float(kTilt, &lean_angle_, 0); // tilt
        level_y_ = 0.0F;
        params->fetch_float(kCursorHeight, &level_y_, 0);
        return 1;
    }
    return 2;
}

// 0x004E5C20
void EditorViewModule::tick_frame()
{
    min_tilt_ = get_global(kEditorCamTiltMin);
    max_tilt_ = get_global(kEditorCamTiltMax);
    const PadSnapshot& pad = input_ptr_->pad_primary();

    // Heading, wrapped into 0..2 pi (the comparison on the unrounded sum, as the original).
    const double heading = (static_cast<double>(get_global(0x8242D878)) * core::at(pad.scaled_stick(), 0)) + heading_;
    heading_ = static_cast<float>(heading);
    if (heading > maths::kTwoPi) {
        double h = heading_;
        h -= maths::kTwoPi;
        while (h > maths::kTwoPi) {
            h -= maths::kTwoPi;
        }
        heading_ = static_cast<float>(h);
    }
    if (heading_ < 0.0F) {
        double h = heading_;
        h += maths::kTwoPi;
        while (h < 0.0F) {
            h += maths::kTwoPi;
        }
        heading_ = static_cast<float>(h);
    }
    // Tilt, within the globals' range.
    const double tilt = -(static_cast<double>(get_global(0x3CFA3A72)) * core::at(pad.scaled_stick(), 1)) + lean_angle_;
    lean_angle_ = static_cast<float>(tilt);
    if (tilt > max_tilt_) {
        lean_angle_ = max_tilt_;
    }
    lean_angle_ = std::max(lean_angle_, min_tilt_);

    // The zoom and height buttons by controller type.
    bool zoom_in = false;
    bool zoom_out = false;
    bool raise = false;
    bool lower = false;
    const int controller = game_config::hardware();
    const uint32_t buttons = input_ptr_->events();
    if (controller == 4 || controller == kControllerPs2Pad) {
        zoom_in = (buttons & kButtonZoomIn) != 0;
        zoom_out = (buttons & kButtonZoomOut) != 0;
        if (park_editing::ParkBuilderMode::sole()->editor_state() == kEdit) {
            raise = (buttons & kButtonRaiseA) != 0;
            lower = (buttons & kButtonLowerA) != 0;
        } else {
            raise = (buttons & kButtonRaiseB) != 0;
            lower = (buttons & kButtonLowerB) != 0;
        }
    } else if (controller == kControllerXbox) {
        if (buttons & kButtonShift) {
            zoom_in = (buttons & kButtonLowerB) != 0;
            zoom_out = (buttons & kButtonRaiseA) != 0;
        } else {
            lower = (buttons & kButtonLowerB) != 0;
            raise = (buttons & kButtonRaiseA) != 0;
        }
    } else {
        zoom_in = pad.shoulder_right_a().held_down() != 0;
        zoom_out = pad.shoulder_right_b().held_down() != 0;
        raise = pad.shoulder_left_a().held_down() != 0;
        lower = pad.shoulder_left_b().held_down() != 0;
    }
    double zoom_rate = get_global(kEditorCamInOutSpeed);
    if (!zoom_in) {
        zoom_rate = zoom_out ? -zoom_rate : 0.0F;
    }
    const double zoom = (zoom_rate * zoom_) + zoom_;
    zoom_ = static_cast<float>(zoom);
    if (zoom < min_zoom_) {
        zoom_ = min_zoom_;
    }
    zoom_ = std::min(zoom_, max_zoom_);
    auto zoom_fraction =
        static_cast<float>((static_cast<double>(zoom_) - min_zoom_) / (static_cast<double>(max_zoom_) - min_zoom_));

    // Raising / lowering the height (by the zoom; slowly when close), not through a ceiling.
    const float near_rate = get_global(0x604961F4);
    auto rise = static_cast<float>(
        ((static_cast<double>(get_global(kEditorCamUpDownSpeedMax)) - near_rate) * zoom_fraction) + near_rate);
    if (zoom_ < kNearZoom) {
        rise = kRiseStep;
    }
    if (!raise) {
        if (!lower) {
            rise = 0.0F;
        } else {
            rise = -rise;
        }
    }
    if ((raise || lower) && rise > 0.0F) {
        const maths::Vec4 from = {.x = cursor_.x, .y = static_cast<float>(static_cast<double>(cursor_.y) + level_y_ + kAbove),
                                  .z = cursor_.z, .w = cursor_.w};
        const maths::Vec4 to = {.x = cursor_.x, .y = from.y - kDown, .z = cursor_.z, .w = cursor_.w};
        Feeler ceiling;
        ceiling.change_origin(from);
        ceiling.change_finish(to);
        if (ceiling.get_hit_shape(1, 0)) {
            const float room = from.y - ceiling.get_point().y;
            if (get_global(kEditorCamMaxHeight) < room) {
                rise = 0.0F;
            }
            if (room < 0.0F) {
                rise = 0.0F;
            }
        } else {
            rise = 0.0F;
        }
    }
    level_y_ = rise + level_y_;

    const modules::Skate* skate = modules::Skate::sole();
    if (in_park_editor()) {
        rail_editor_moving(0);
    }

    // The cursor kept under anything above it, and the height under any ceiling.
    Feeler feeler;
    {
        const float height = level_y_;
        const maths::Vec4 above = {.x = cursor_.x,
                                   .y = static_cast<float>(static_cast<double>(height) + cursor_.y),
                                   .z = cursor_.z,
                                   .w = static_cast<float>(static_cast<double>(cursor_.w) + 1.0F)};
        feeler.change_origin(above);
        feeler.change_finish(cursor_);
        if (feeler.get_hit_shape(1, 0)) {
            level_y_ = static_cast<float>((static_cast<double>(cursor_.y) + level_y_) - feeler.get_point().y);
            cursor_ = feeler.get_point();
        }
        feeler.change_origin(cursor_);
        const float up = level_y_ + kAbove;
        const maths::Vec4 top = {.x = cursor_.x,
                                 .y = up + cursor_.y,
                                 .z = cursor_.z,
                                 .w = static_cast<float>(static_cast<double>(cursor_.w) + 1.0F)};
        feeler.change_finish(top);
        if (feeler.get_hit_shape(1, 0)) {
            level_y_ = static_cast<float>((static_cast<double>(feeler.get_point().y) - cursor_.y) - kAbove);
        }
    }
    if (rise < 0.0F && min_height_ <= (static_cast<double>(level_y_) - rise) + kHeightSlack && level_y_ < min_height_) {
        level_y_ = min_height_;
    }
    level_y_ = std::max(level_y_, 0.0F);

    // The heading's axes.
    const float heading_now = heading_;
    right_side_ = {.x = static_cast<float>(std::cos(static_cast<double>(heading_now))),
                   .y = 0.0F,
                   .z = static_cast<float>(-std::sin(static_cast<double>(heading_now))),
                   .w = 1.0F};
    forward_ = {.x = static_cast<float>(std::sin(static_cast<double>(heading_now))),
                .y = 0.0F,
                .z = static_cast<float>(std::cos(static_cast<double>(heading_now))),
                .w = 1.0F};
    const float near_speed = get_global(0x83BDE786);
    double speed =
        ((static_cast<double>(get_global(kEditorCamMoveSpeedMax)) - near_speed) * zoom_fraction) + near_speed;
    if (zoom_ < kNearZoom) {
        speed = 1.0F;
    }

    // The left stick (or the d-pad) moves the cursor along the axes.
    auto side = static_cast<float>(static_cast<double>(core::at(pad.stick(), 2)) * kStickScale);
    double ahead = static_cast<double>(core::at(pad.stick(), 3)) * kStickScale;
    if (std::fabs(side) < kDeadZone) {
        if (pad.left_side().held_down()) {
            side = -kMoveStep;
        }
        if (pad.right_side().held_down()) {
            side = kMoveStep;
        }
    }
    if (std::fabs(ahead) < kDeadZone) {
        if (pad.up_dir().held_down()) {
            ahead = -kMoveStep;
        }
        if (pad.btn_down().held_down()) {
            ahead = kMoveStep;
        }
    }
    auto a1 = static_cast<float>(ahead * forward_.w);
    auto a2 = static_cast<float>(ahead * forward_.z);
    auto a3 = static_cast<float>(ahead * forward_.y);
    const double a4 = ahead * forward_.x;
    auto b1 = static_cast<float>(a1 * speed);
    auto b2 = static_cast<float>(a2 * speed);
    auto b3 = static_cast<float>(a3 * speed);
    auto b4 = static_cast<float>(a4 * speed);
    const double c1 = static_cast<double>(side) * right_side_.w;
    const double c2 = static_cast<double>(side) * right_side_.z;
    const float c3 = side * right_side_.y;
    const float c4 = side * right_side_.x;
    auto d1 = static_cast<float>(c1 * speed);
    auto d2 = static_cast<float>(c2 * speed);
    auto d3 = static_cast<float>(c3 * speed);
    const double x0 = speed * c4;
    const maths::Vec4 previous = cursor_;
    cursor_.x = static_cast<float>((x0 + b4) + cursor_.x);
    cursor_.y = static_cast<float>((static_cast<double>(d3) + b3) + cursor_.y);
    cursor_.z = static_cast<float>((static_cast<double>(d2) + b2) + cursor_.z);
    cursor_.w = static_cast<float>((static_cast<double>(d1) + b1) + cursor_.w);
    if (!place_cursor(previous, get_global(kEditorCamCursorCollisionFirstUpOffset))) {
        cursor_ = previous;
    }
    if (skate->level() == kLoadSk5Ed) {
        rail_editor_moving(1);
    }

    // The camera: the focus follows the cursor (its height eased), the camera at the zoom along the tilt.
    focus_.x = cursor_.x;
    focus_.z = cursor_.z;
    const float ease = get_global(0x725AD02C);
    focus_.y = static_cast<float>((((static_cast<double>(cursor_.y) + level_y_) - focus_.y) * ease) + focus_.y);
    const float tilt_now = lean_angle_;
    auto at_x = static_cast<float>(std::cos(static_cast<double>(tilt_now)) * forward_.x);
    auto at_z = static_cast<float>(std::cos(static_cast<double>(tilt_now)) * forward_.z);
    auto at_y = static_cast<float>(std::sin(static_cast<double>(tilt_now)));
    maths::Mat4 look;
    core::at(look.row, 0) = {.x = right_side_.x, .y = right_side_.y, .z = right_side_.z, .w = 0.0F};
    core::at(look.row, 1) = {.x = static_cast<float>((static_cast<double>(at_y) * right_side_.z) -
                                                     (static_cast<double>(at_z) * right_side_.y)),
                             .y = static_cast<float>((static_cast<double>(at_z) * right_side_.x) -
                                                     (static_cast<double>(at_x) * right_side_.z)),
                             .z = static_cast<float>((static_cast<double>(at_x) * right_side_.y) -
                                                     (static_cast<double>(at_y) * right_side_.x)),
                             .w = 0.0F};
    core::at(look.row, 2) = {.x = at_x, .y = at_y, .z = at_z, .w = 0.0F};
    core::at(look.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    entity()->basis_frame() = look;
    const float zoom_now = zoom_;
    const float off_w = previous.w * zoom_now; // (the cursor's w from before the move, as the original reuses it)
    const float off_z = at_z * zoom_now;
    const float nudge_along_y = at_y * zoom_now;
    const float side_shift_x = at_x * zoom_now;
    viewer_.x = side_shift_x + focus_.x;
    viewer_.y = focus_.y + nudge_along_y;
    viewer_.z = focus_.z + off_z;
    viewer_.w = focus_.w + off_w;
    viewer_.w = 1.0F;
    entity()->at_point() = viewer_;
    resync_ground_shadow();
}

} // namespace objects
