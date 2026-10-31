// The WalkCamera component (GameObjects::FootView): see walk_camera.h. From the disassembly of
// 0x00524C20-0x00525E3F (and 0x00525F70).

#include "game/rider/walk_camera.h"
#include "engine/gfx/d3dx.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/view_cam.h"
#include "engine/maths/rotation_blend.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/camera_util.h"
#include "engine/objects/compositemgr.h"
#include "engine/script/param_struct.h"
#include "game/rider/walk_physics.h"
#include "engine/maths/x87_math.h"
#include "platform/stopwatch.h"

namespace objects {

namespace {

template <typename T>
T& get_at(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

constexpr uint32_t kWalkCamera = 0xC7D37EC2;
constexpr uint32_t kWalk = 0x726E85AA, kSkaterPhysicsControl = 0xD9151D4D, kCameraLookAround = 0x81C7122D,
                   kCamera = 0xC4E311FA, kSkaterCamera = 0x5E43A604, kInput = 0x27D7CD28;
constexpr uint32_t kSkaterId = 0x4C48DA58; // (an object id; the name isn't recovered)
constexpr uint32_t kDrainQueueCmd = 0x73FEBD0F, kReset = 0xD1A485D1;
constexpr uint32_t kParameters = 0x1CDA02AE; // walk_camera_parameters
constexpr uint32_t kLockAngle = 0xB8DA8A73, kFullSlerpSpeed = 0xBCEF6DDA, kMinSlerpSpeed = 0x824349E5,
                   kDpadFullSlerpSpeed = 0x73DA1EC0, kDpadMinSlerpSpeed = 0x0DA5CAD3, kFacingControl = 0xF6F69DC8,
                   kMatrixSlerpRate = 0x0C39B639, kRunSlerpFactor = 0xD25348EB, kControlSlerpFactor = 0xB2DA3C87,
                   kFlushSlerpFactor = 0x7178E048;

constexpr float kDegrees = 0.01745329238474369f;  // 0x00645464
constexpr float kAligned = 0.9900000095367432f;   // 0x00645450
constexpr float kNearHeight = 3.0f;               // 0x006467A8
constexpr float kTiltOffset = 0.628000020980835f; // 0x0064BB00
constexpr float kTiltScale = 1.5923566818237305f; // 0x0064BAFC
constexpr float kTiltRange = 0.6000000238418579f; // 0x006463C4
constexpr float kTiltBase = 0.4000000059604645f;  // 0x0064BAF8

// D3DXMatrixMultiply (linked later; 0x005BFA8C).
void matrix_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b) { d3dx::matrix_multiply(out, a, b); }

// The right stick's turn of the facing (0x00525468-): the sine rounded to float, the cosine kept at full
// precision: x' = z * sin + cos * x, z' = -(sin * x) + z * cos.
void turn_by_stick(double angle, float* x, float* z)
{
    float sine;
    __asm {
        mov ecx, x
        mov edx, z
        fld angle
        fld st(0)
        fsin
        fstp sine
        fcos
        fld sine
        fmul dword ptr [ecx]
        fchs
        fld dword ptr [edx]
        fmul st, st(2)
        faddp st(1), st
        fld dword ptr [edx]
        fmul sine
        fxch st(2)
        fmul dword ptr [ecx]
        faddp st(2), st
        fxch st(1)
        fstp dword ptr [ecx]
        fstp dword ptr [edx]
    }
}

} // namespace

// 0x00524D70
maths::Vec4* __cdecl lerp_by_range(maths::Vec4* out, const maths::Vec4* a, const maths::Vec4* b, float t, float low,
                                   float high)
{
    double dx = static_cast<double>(b->x) - a->x;
    double dy = static_cast<double>(b->y) - a->y;
    float dz = b->z - a->z;
    float dw = b->w - a->w;
    float f = static_cast<float>((static_cast<double>(t) - low) / (static_cast<double>(high) - low));
    float sx = static_cast<float>(dx * f);
    double sy = dy * f;
    float sz = dz * f;
    float sw = dw * f;
    float x = sx + a->x;
    float y = static_cast<float>(sy + a->y);
    float z = sz + a->z;
    float w = sw + a->w;
    out->w = w;
    out->x = x;
    out->y = y;
    out->z = z;
    return out;
}

// 0x00525DC0
ObjectModule* __cdecl FootView::fabricate() { return new FootView; }

// (the constructor: inlined in fabricate in the original)
FootView::FootView()
{
    kind_tag_ = kWalkCamera;
    aim_point_ = {0.0f, 0.0f, 0.0f, 1.0f};
}

// 0x00524C20 (0x00524F20 the deleting one)
FootView::~FootView() {}

// 0x00524C50
void FootView::bind_rider(GameEntity* skater)
{
    if (!skater) {
        skater_ = nullptr;
        walk_ = nullptr;
        physics_control_ = nullptr;
        return;
    }
    skater_ = skater;
    walk_ = static_cast<FootMovement*>(skater->fetch_module(kWalk));
    physics_control_ = reinterpret_cast<uint8_t*>(skater_->fetch_module(kSkaterPhysicsControl));
}

// 0x00524F50
void FootView::configure_from_data(script::Struct* params)
{
    uint32_t id = 0;
    params->read_hash_value(kSkaterId, &id, 1);
    bind_rider(static_cast<GameEntity*>(object_hub_slot()->look_up_entity(id)));
    turn_cosine_ = 1.0f;
    zoom_ = 1.0f;
    orientation_ = entity_->basis_frame_;
}

// 0x00524FB0
void FootView::seal()
{
    free_look_ = static_cast<FreeLookModule*>(entity_->fetch_module(kCameraLookAround));
    viewer_ = entity_->fetch_module(kCamera);
    skater_camera_ = reinterpret_cast<uint8_t*>(entity_->fetch_module(kSkaterCamera));
}

// 0x00524C30
void FootView::reset_now()
{
    snap_next_ = true;
    tick_frame();
    snap_next_ = false;
}

// 0x005250A0
void FootView::apply_queued_change()
{
    flush_ = true;
    get_at<bool>(walk_, 0x70C) = true;
    free_look_->heading_ = 0.0f;
    free_look_->lean_angle_ = 0.0f;
    free_look_->factor_ = 1.0f;
    free_look_->locked_ = false;
}

// 0x00525D40
int FootView::invoke_method_by_name(uint32_t name, script::Struct* /*params*/, script::Script* /*script*/)
{
    if (name == kDrainQueueCmd) {
        apply_queued_change();
        return 1;
    }
    if (name == kReset) {
        reset_now();
        return 1;
    }
    return 2;
}

// 0x00525F70
void FootView::change_forced_heading(const maths::Vec4* facing, float rate)
{
    override_ = true;
    forced_heading_ = *facing;
    override_rate_ = rate;
}

// 0x00525060
void FootView::save_state(WalkCameraState* state)
{
    state->orientation = orientation_;
    state->target = aim_point_;
    state->turn_cosine_ = turn_cosine_;
    state->zoom = zoom_;
}

// 0x00524FF0
void FootView::restore_state(const WalkCameraState* state)
{
    aim_point_ = state->target;
    orientation_ = state->orientation;
    turn_cosine_ = state->turn_cosine_;
    zoom_ = state->zoom;
    flush_ = false;
    get_at<bool>(walk_, 0x70C) = false;
    instant_frames_ = 0;
    free_look_->heading_ = 0.0f;
    free_look_->locked_ = false;
    override_ = false;
}

// 0x00524EF0
float __stdcall FootView::get_param(uint32_t name)
{
    float value = 0.0f; // (the original's is uninitialised: the value is asserted)
    script::read_sub_struct(kParameters)->fetch_float(name, &value, 1);
    return value;
}

// 0x005250E0
void FootView::fetch_goal(maths::Vec4* out, bool instant)
{
    if (instant) {
        aim_point_ = skater_->at_point_;
    } else {
        float across = static_cast<float>(frame_blend(get_at<float>(skater_camera_, 0xE4),
                                                      static_cast<float>(frame_clock::get_slomo_frame_seconds())));
        float up = static_cast<float>(frame_blend(get_at<float>(skater_camera_, 0xE8),
                                                  static_cast<float>(frame_clock::get_slomo_frame_seconds())));
        const maths::Vec4& skater = skater_->at_point_;
        float x = aim_point_.x, y = aim_point_.y, z = aim_point_.z;
        aim_point_.w = 1.0f;
        aim_point_.x = static_cast<float>((static_cast<double>(skater.x) - x) * across + x);
        aim_point_.y = static_cast<float>((static_cast<double>(skater.y) - y) * up + y);
        aim_point_.z = static_cast<float>((static_cast<double>(skater.z) - z) * across + z);
    }
    *out = aim_point_;
}

// 0x00524CB0
void FootView::get_zoomed_offsets(float* height, float* distance)
{
    float factor = 1.0f;
    uint32_t factor_bits;
    std::memcpy(&factor_bits, &free_look_->factor_, 4);
    if (free_look_->turning_ && factor_bits != 0x3F800000)
        factor = free_look_->factor_;
    double zoom = (static_cast<double>(factor) - zoom_) * get_at<float>(skater_camera_, 0x110) + zoom_;
    zoom_ = static_cast<float>(zoom);
    double behind = zoom * get_at<float>(skater_camera_, 0xC4);
    *distance = static_cast<float>(behind);
    float tilt = free_look_->lean_angle_;
    if (tilt < 0.0f)
        *distance = static_cast<float>(
            (((static_cast<double>(tilt) + kTiltOffset) * kTiltScale) * kTiltRange + kTiltBase) * behind);
    float above = get_at<float>(skater_camera_, 0xD0);
    if (zoom_ < 1.0f)
        *height = static_cast<float>((static_cast<double>(above) - kNearHeight) * zoom_ + kNearHeight);
    else
        *height = above;
}

// 0x005251D0
void FootView::tick_frame()
{
    if (!skater_)
        return;
    if (physics_control_ && get_at<bool>(get_at<uint8_t*>(physics_control_, 0x2C), 0x118)) {
        entity_->composite_flags_ |= kCompositeFlagPaused;
        return;
    }
    if (skater_->composite_flags_ & kCompositeFlagTeleported)
        instant_frames_ = 3;
    bool instant;
    if (instant_frames_ > 0) {
        --instant_frames_;
        instant = true;
    } else {
        instant = snap_next_;
    }
    get_at<gfx::ViewCam*>(viewer_, 0x18)->remember_prev_pos();
    float frame = static_cast<float>(frame_clock::get_slomo_frame_seconds());
    float turn = get_at<float>(entity_->fetch_module(kInput), 0x230);
    entity_->basis_frame_ = orientation_;

    // Where to look: along the camera's flat facing, swung round to the skater's.
    const maths::Vec4& at = entity_->basis_frame_.row[2];
    maths::Vec4 facing = {-at.x, 0.0f, -at.z, -at.w};
    facing.make_unit();
    const maths::Vec4& skater_at = skater_->basis_frame_.row[2];
    maths::Vec4 skater_facing = {skater_at.x, 0.0f, skater_at.z, skater_at.w};
    skater_facing.make_unit();
    if (flush_) {
        facing = skater_facing;
    } else {
        bool swing = false;
        if (turn == 0.0f) {
            float cosine = static_cast<float>(static_cast<double>(skater_facing.x) * facing.x +
                                              static_cast<double>(skater_facing.z) * facing.z +
                                              static_cast<double>(skater_facing.y) * facing.y);
            if (!x87::cos_above(static_cast<double>(get_param(kLockAngle)) * kDegrees, cosine)) {
                float full, least;
                if (!walk_->using_dpad()) {
                    full = get_param(kFullSlerpSpeed);
                    least = get_param(kMinSlerpSpeed);
                } else {
                    full = get_param(kDpadFullSlerpSpeed);
                    least = get_param(kDpadMinSlerpSpeed);
                }
                const maths::Vec4& velocity = skater_->vel_;
                double speed = x87::get_sqrt(static_cast<double>(velocity.x) * velocity.x +
                                             static_cast<double>(velocity.z) * velocity.z);
                float speed_f = static_cast<float>(speed);
                if (!(speed < full)) {
                    swing = true;
                } else if (speed_f > least) {
                    maths::Vec4 lerped;
                    facing = *lerp_by_range(&lerped, &facing, &skater_facing, speed_f, least, full);
                    facing.make_unit();
                }
            }
        }
        if (swing || snap_next_)
            facing = skater_facing;
    }
    if (!(turn == 0.0f) && !flush_)
        turn_by_stick(static_cast<double>(get_param(kFacingControl)) * turn, &facing.x, &facing.z);
    if (override_) {
        float x = forced_heading_.x;
        facing.y = forced_heading_.y;
        facing.z = forced_heading_.z;
        facing.w = forced_heading_.w;
        double heading = free_look_->heading_;
        float s = x87::get_sin_f(heading);
        float c = x87::cos_f(heading);
        float z = facing.z;
        float turned_z = static_cast<float>(-(static_cast<double>(s) * x) + static_cast<double>(z) * c);
        facing.x = static_cast<float>(static_cast<double>(z) * s + static_cast<double>(c) * x);
        facing.z = turned_z;
    }

    // The orientation that looks along it, tilted down; its right and at rows negated.
    maths::Mat4 look;
    look.row[0] = {facing.z, 0.0f, -facing.x, 1.0f};
    look.row[1] = {0.0f, 1.0f, 0.0f, 1.0f};
    look.row[2] = facing;
    look.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    maths::Mat4 tilt;
    maths::make_pitch_matrix(tilt, free_look_->lean_angle_ + get_at<float>(skater_camera_, 0x12C));
    matrix_multiply(&look, &tilt, &look);
    look.row[0] = {-look.row[0].x, -look.row[0].y, -look.row[0].z, -look.row[0].w};
    look.row[2] = {-look.row[2].x, -look.row[2].y, -look.row[2].z, -look.row[2].w};
    maths::Vec4 skater_up = skater_->basis_frame_.row[1];
    maths::Mat4& matrix = entity_->basis_frame_;
    if (instant) {
        matrix = look;
    } else {
        auto aligned = [](const maths::Vec4& a, const maths::Vec4& b) {
            return static_cast<double>(a.z) * b.z + static_cast<double>(a.y) * b.y + static_cast<double>(a.x) * b.x >
                   kAligned;
        };
        if (aligned(look.row[0], matrix.row[0]) && aligned(look.row[1], matrix.row[1]) &&
            aligned(look.row[2], matrix.row[2])) {
            if (flush_) {
                flush_ = false;
                get_at<bool>(walk_, 0x70C) = false;
            }
        } else {
            // Slerped toward it: at matrix_slerp_rate, faster running (and twice that hanging or climbing), with
            // the stick, flushing; an override's own rate.
            maths::ArcRotationBlender slerp(matrix, look);
            float base;
            script::read_sub_struct(kParameters)->fetch_float(kMatrixSlerpRate, &base, 1);
            float rate = base;
            if (get_at<bool>(walk_, 0x17A4))
                rate = static_cast<float>(static_cast<double>(get_param(kRunSlerpFactor)) * base);
            int state = get_at<int>(walk_, 0x18);
            if (state == 4 || state == 3)
                rate = rate + rate;
            if (!(turn == 0.0f) && !flush_) {
                float control = get_param(kControlSlerpFactor);
                rate = static_cast<float>(static_cast<double>(control) * std::fabs(turn) * rate);
            }
            if (flush_) {
                get_at<bool>(walk_, 0x70C) = true;
                rate = static_cast<float>(static_cast<double>(get_param(kFlushSlerpFactor)) * rate);
            }
            if (override_) {
                if (flush_) {
                    float flush = get_param(kFlushSlerpFactor);
                    rate = static_cast<float>(static_cast<double>(get_param(kMatrixSlerpRate)) * flush);
                } else {
                    rate = override_rate_;
                }
            }
            slerp.fetch_mat(matrix, static_cast<float>(frame_blend(rate, frame)));
            const maths::Vec4& now = matrix.row[2];
            const maths::Vec4& before = orientation_.row[2];
            turn_cosine_ =
                static_cast<float>(static_cast<double>(now.z) * before.z + static_cast<double>(now.y) * before.y +
                                   static_cast<double>(now.x) * before.x);
        }
    }
    orientation_ = matrix;

    // Where to be: behind and above the point followed, looking at a point above the skater.
    maths::Vec4 target;
    fetch_goal(&target, instant);
    float height, distance;
    get_zoomed_offsets(&height, &distance);
    double up_x = static_cast<double>(skater_up.x) * height;
    double up_y = static_cast<double>(skater_up.y) * height;
    double up_z = static_cast<double>(skater_up.z) * height;
    double up_w = static_cast<double>(skater_up.w) * height;
    float up_xf = static_cast<float>(up_x), up_zf = static_cast<float>(up_z), up_wf = static_cast<float>(up_w);
    const maths::Vec4& look_at = matrix.row[2];
    double back_x = static_cast<double>(distance) * look_at.x;
    double back_y = static_cast<double>(distance) * look_at.y;
    float back_z = distance * look_at.z;
    float back_w = distance * look_at.w;
    float step_x = static_cast<float>(back_x + up_xf);
    float step_y = static_cast<float>(back_y + up_y);
    double step_z = back_z + up_z;
    double step_w = static_cast<double>(back_w) + up_wf;
    target.x = step_x + target.x;
    target.y = target.y + step_y;
    target.z = static_cast<float>(target.z + step_z);
    target.w = static_cast<float>(target.w + step_w);
    const maths::Vec4& skater_pos = skater_->at_point_;
    maths::Vec4 focus = {up_xf + skater_pos.x, static_cast<float>(up_y + skater_pos.y), up_zf + skater_pos.z,
                         up_wf + skater_pos.w};

    maths::Mat4 aim;
    maths::Vec4& aim_at = aim.row[2];
    aim_at = {focus.x - target.x, focus.y - target.y, focus.z - target.z, focus.w - target.w};
    aim_at.make_unit();
    const maths::Vec4 up = matrix.row[1];
    maths::Vec4& right = aim.row[0];
    right = {static_cast<float>(static_cast<double>(up.y) * aim_at.z - static_cast<double>(up.z) * aim_at.y),
             static_cast<float>(static_cast<double>(up.z) * aim_at.x - static_cast<double>(up.x) * aim_at.z),
             static_cast<float>(static_cast<double>(up.x) * aim_at.y - static_cast<double>(up.y) * aim_at.x), 0.0f};
    right.make_unit();
    maths::Vec4& aim_up = aim.row[1];
    aim_up = {static_cast<float>(static_cast<double>(right.z) * aim_at.y - static_cast<double>(right.y) * aim_at.z),
              static_cast<float>(static_cast<double>(right.x) * aim_at.z - static_cast<double>(right.z) * aim_at.x),
              static_cast<float>(static_cast<double>(right.y) * aim_at.x - static_cast<double>(right.x) * aim_at.y),
              0.0f};
    aim_up.make_unit();
    matrix.row[0] = {-right.x, -right.y, -right.z, -right.w};
    matrix.row[1] = aim_up;
    matrix.row[2] = {-aim_at.x, -aim_at.y, -aim_at.z, -aim_at.w};
    matrix.row[0].w = 0.0f;
    matrix.row[1].w = 0.0f;
    matrix.row[2].w = 0.0f;
    matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};

    // The collision check pulls the camera toward the point `distance` ahead of it (and the walk's offset).
    double ahead_x = static_cast<double>(distance) * matrix.row[2].x;
    double ahead_y = static_cast<double>(distance) * matrix.row[2].y;
    float ahead_z = distance * matrix.row[2].z;
    float ahead_w = distance * matrix.row[2].w;
    maths::Vec4 clear = {static_cast<float>(target.x - ahead_x), static_cast<float>(target.y - ahead_y),
                         target.z - ahead_z, target.w - ahead_w};
    maths::Vec4 offset;
    walk_->get_camera_offset(&offset);
    maths::Vec4 pull = {clear.x + offset.x, clear.y + offset.y, clear.z + offset.z, clear.w + offset.w};
    avoid_camera_collisions(&target, &matrix, &pull, &focus, true, true);
    target.w = 1.0f;
    entity_->at_point_ = target;
    if (instant)
        get_at<gfx::ViewCam*>(viewer_, 0x18)->remember_prev_pos();
    get_at<maths::Mat4>(skater_camera_, 0x74) = matrix;
}

} // namespace objects
