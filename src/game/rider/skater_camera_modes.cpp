// The skater camera component's (skater_camera_component.h) destructor body, Finalize, the state taken over from
// another camera, the distances' lerps and its camera modes. From the disassembly of 0x004F9970-0x004F9E7E.

#include <bit>
#include <cstdint>
#include <cstring>

#include "engine/memory/manager.h"
#include "engine/gfx/view_cam.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/script/param_struct.h"
#include "game/rider/skater_camera_component.h"
#include "game/rider/walk_camera.h"
#include "engine/gfx/viewport_list.h"
#include "engine/objects/camera_component.h"
#include "base/core/down_cast.h"
#include "base/maths/trig.h"

namespace objects {

namespace {
constexpr int kModeBackA = 5;                      // camera modes that look back, right and left
constexpr int kModeBackB = 6;
constexpr int kModeRightA = 7;
constexpr int kModeRightB = 8;
constexpr int kModeLeftA = 11;
constexpr int kModeLeftB = 12;

constexpr uint32_t kCameraLookaround = 0x81C7122D, kCamera = 0xC4E311FA;

constexpr uint32_t kHorizFov = 0x94DF1603, kBehind = 0x52E6C41B, kAbove = 0x0B96AE2D, kTilt = 0xE3C07609,
                   kOriginOffset = 0x012F6992, kLipTrickTilt = 0x7F0032AC, kLipTrickAbove = 0xADB6390E,
                   kSlerp = 0xF54FB9C5, kVertAirSlerp = 0x2FA11029, kVertAirLandedSlerp = 0x042FD4A0,
                   kLerpXz = 0xAE47899B, kLerpY = 0xE1E4E104, kVertAirLerpXz = 0xF386F4D9,
                   kHalfpipeAirBlendY = 0x4882A1FE, kLookRate = 0xBEF0D195, kWallrideOffsetAngle = 0x3396BE2A,
                   kZoomBlend = 0x748743A7, kRailZoom = 0x5A7F5CC5, kBigAirTrickZoom = 0xD790B83D,
                   kLipTrickZoom = 0xD414C22E, kName = 0xA1DC81F9;

constexpr float kFeet = 12.0F; // 0x0064546C
constexpr float kHalf = 0.5F;  // 0x006453BC
constexpr float kHalfPi = 1.57079637F;

float one_frame()
{
    const uint32_t bits = 0x3C888889; // 0x0064BA64 (1/60)
    auto value = std::bit_cast<float>(bits);
    return value;
}

// The screen mode (0x004A0710: 1 a vertical split, 2 a horizontal one).
int screen_mode() { return render::get_view_layout(); }

} // namespace

// 0x004F99F0 (0x004FA590 the deleting one)
RiderChaseView::~RiderChaseView()
{
    maths::ArcRotationBlender* slerp = arc_blender_;
    memory::Hub::dealloc(slerp);
}

// 0x004F9A20
void RiderChaseView::seal()
{
    free_look_module_ = core::down_cast<FreeLookModule>(entity()->fetch_module(kCameraLookaround));
}

// 0x004F9A40
void RiderChaseView::change_phase(const WalkCameraState* state)
{
    std::memcpy(orientation_.data(), &state->orientation, sizeof(orientation_));
    std::memcpy(&aim_point_, &state->target, sizeof(aim_point_));
    turn_cosine_ = state->turn_cosine_;
    zoom_ = state->zoom;
    settle_time_ = 0.0F;
    instant_frames_ = 0;
    free_look_module_->set_heading(0.0F);
    free_look_module_->assign_locked(false);
}

// 0x004F9970
void RiderChaseView::update_lerps()
{
    if (behind_time_ > 0.0F) {
        behind_time_ = static_cast<float>(static_cast<double>(behind_time_) - one_frame());
        behind_ = static_cast<float>(static_cast<double>(behind_step_) + behind_);
    } else {
        behind_time_ = 0.0F;
    }
    if (above_time_ > 0.0F) {
        above_time_ = static_cast<float>(static_cast<double>(above_time_) - one_frame());
        above_ = static_cast<float>(static_cast<double>(above_step_) + above_);
    } else {
        above_time_ = 0.0F;
    }
}

// 0x004F9AE0
void RiderChaseView::change_regime(CameraRegime camera_regime)
{
    const auto mode = camera_regime.mode;
    const auto time = camera_regime.time;
    auto* camera = entity()->fetch_module(kCamera);
    const int screen = screen_mode();
    const char* array_name = "Skater_Camera_Array";
    if (screen == 1) {
        array_name = "Skater_Camera_2P_Vert_Array";
    } else if (screen == 2) {
        array_name = "Skater_Camera_2P_Horiz_Array";
    }
    script::Array* modes = script::fetch_list(array_name);
    script::Struct* params = script::read_sub_struct(modes->fetch_part(static_cast<uint32_t>(mode)));
    mode_sel_ = mode;
    lip_trick_tilt_ = 0.0F;
    float fov = 0.0F;
    float behind = 0.0F;
    float above = 0.0F;
    float tilt = 0.0F;
    float offset = 0.0F;
    params->fetch_float(kHorizFov, &fov, 0);
    params->fetch_float(kBehind, &behind, 0);
    params->fetch_float(kAbove, &above, 0);
    params->fetch_float(kTilt, &tilt, 0);
    params->fetch_float(kOriginOffset, &offset, 0);
    params->fetch_float(kLipTrickTilt, &lip_trick_tilt_, 0);
    params->fetch_float(kLipTrickAbove, &lip_trick_above_, 0);
    params->fetch_float(kSlerp, &slerp_, 0);
    params->fetch_float(kVertAirSlerp, &vert_air_slerp_, 0);
    params->fetch_float(kVertAirLandedSlerp, &vert_air_landed_slerp_, 0);
    params->fetch_float(kLerpXz, &lerp_xz_, 0);
    params->fetch_float(kLerpY, &lerp_y_, 0);
    params->fetch_float(kVertAirLerpXz, &vert_air_lerp_xz_, 0);
    params->fetch_float(kHalfpipeAirBlendY, &halfpipe_up_blend_, 0);
    params->fetch_float(kLookRate, &look_rate_, 0);
    params->fetch_float(kWallrideOffsetAngle, &wallride_offset_angle_, 0);
    float zoom_lerp = 0.0F;
    rail_zoom_factor_ = 1.0F;
    lip_trick_zoom_ = 1.0F;
    big_air_trick_zoom_ = 1.0F;
    params->fetch_float(kZoomBlend, &zoom_lerp, 0);
    params->fetch_float(kRailZoom, &rail_zoom_factor_, 0);
    params->fetch_float(kBigAirTrickZoom, &big_air_trick_zoom_, 0);
    params->fetch_float(kLipTrickZoom, &lip_trick_zoom_, 0);
    zoom_ease_rate_ = zoom_lerp;
    const char* name = nullptr;
    params->fetch_text(kName, &name, 0);
    origin_offset_ = static_cast<float>(static_cast<double>(offset) * kFeet);
    horiz_fov_ = fov;
    lean_angle_ = tilt;
    const double behind_feet = static_cast<double>(behind) * kFeet;
    if (time > 0.0F) {
        // lerped there a frame (1/60 s) at a time
        auto behind_target = static_cast<float>(behind_feet);
        auto above_target = static_cast<float>(static_cast<double>(above) * kFeet);
        behind_time_ = time;
        above_time_ = time;
        const double per_frame = static_cast<double>(one_frame()) / time;
        behind_step_ = static_cast<float>((static_cast<double>(behind_target) - behind_) * per_frame);
        above_step_ = static_cast<float>((static_cast<double>(above_target) - above_) * per_frame);
    } else {
        behind_ = static_cast<float>(behind_feet);
        behind_time_ = 0.0F;
        above_time_ = 0.0F;
        above_ = static_cast<float>(static_cast<double>(above) * kFeet);
    }
    // The camera component's camera (+0x18): its field of view (half across a vertical split).
    gfx::ViewCam* view = core::down_cast<objects::ViewpointModule>(camera)->view_camera().get();
    view->change_h_fov(screen == 1 ? static_cast<float>(static_cast<double>(fov) * kHalf) : fov);
    float heading = 0.0F;
    switch (mode) {
    case kModeBackA:
    case kModeBackB:
        heading = maths::kPi;
        break;
    case kModeRightA:
    case kModeRightB:
        heading = kHalfPi;
        break;
    case kModeLeftA:
    case kModeLeftB:
        heading = -kHalfPi;
        break;
    default:
        heading = 0.0F;
        break;
    }
    free_look_module_->set_mode_heading(heading);
}

} // namespace objects
