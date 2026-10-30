// The skater camera component's (skater_camera_component.h) destructor body, Finalize, the state taken over from
// another camera, the distances' lerps and its camera modes. From the disassembly of 0x004F9970-0x004F9E7E.

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/gfx/camera.h"
#include "game/obj/camera_lookaround.h"
#include "game/script/struct.h"
#include "game/skater/skater_camera_component.h"
#include "game/skater/walk_camera.h"
#include "game/gfx/viewport_list.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x0064BA74);

constexpr uint32_t kCameraLookaround = 0x81C7122D, kCamera = 0xC4E311FA;

constexpr uint32_t kHorizFov = 0x94DF1603, kBehind = 0x52E6C41B, kAbove = 0x0B96AE2D, kTilt = 0xE3C07609,
                   kOriginOffset = 0x012F6992, kLipTrickTilt = 0x7F0032AC, kLipTrickAbove = 0xADB6390E,
                   kSlerp = 0xF54FB9C5, kVertAirSlerp = 0x2FA11029, kVertAirLandedSlerp = 0x042FD4A0,
                   kLerpXZ = 0xAE47899B, kLerpY = 0xE1E4E104, kVertAirLerpXZ = 0xF386F4D9, kVertAirLerpY = 0x4882A1FE,
                   kLookRate = 0xBEF0D195, kWallrideOffsetAngle = 0x3396BE2A, kZoomLerp = 0x748743A7,
                   kGrindZoom = 0x5A7F5CC5, kBigAirTrickZoom = 0xD790B83D, kLipTrickZoom = 0xD414C22E,
                   kName = 0xA1DC81F9;

constexpr float kFeet = 12.0f;  // 0x0064546C
constexpr float kHalf = 0.5f;   // 0x006453BC
constexpr float kPi = 3.14159274f, kHalfPi = 1.57079637f;

float OneFrame()
{
    uint32_t bits = 0x3C888889; // 0x0064BA64 (1/60)
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// The screen mode (0x004A0710: 1 a vertical split, 2 a horizontal one).
int ScreenMode() { return Nx::GetScreenMode(); }

} // namespace

// 0x004F99F0
void CSkaterCameraComponent::DestroyBody()
{
    Mth::SlerpInterpolator* slerp = mp_slerp;
    m_vtable = kVtable;
    Mem::Manager::Get()->Free(slerp);
    CBaseComponent::DestroyBody();
}

// 0x004F9A20
void CSkaterCameraComponent::Finalize()
{
    mp_lookAround = static_cast<CCameraLookAroundComponent*>(m_object->GetComponent(kCameraLookaround));
}

// 0x004F9A40
void CSkaterCameraComponent::SetState(const WalkCameraState* state)
{
    std::memcpy(m_orientation, &state->orientation, sizeof(m_orientation));
    std::memcpy(&m_target, &state->target, sizeof(m_target));
    m_turnCosine = state->turnCosine;
    m_zoom = state->zoom;
    m_settleTime = 0.0f;
    m_instantFrames = 0;
    mp_lookAround->m_heading = 0.0f;
    mp_lookAround->m_locked = false;
}

// 0x004F9970
void CSkaterCameraComponent::UpdateLerps()
{
    if (m_behindTime > 0.0f) {
        m_behindTime = static_cast<float>(static_cast<double>(m_behindTime) - OneFrame());
        m_behind = static_cast<float>(static_cast<double>(m_behindStep) + m_behind);
    } else {
        m_behindTime = 0.0f;
    }
    if (m_aboveTime > 0.0f) {
        m_aboveTime = static_cast<float>(static_cast<double>(m_aboveTime) - OneFrame());
        m_above = static_cast<float>(static_cast<double>(m_aboveStep) + m_above);
    } else {
        m_aboveTime = 0.0f;
    }
}

// 0x004F9AE0
void CSkaterCameraComponent::SetMode(int mode, float time)
{
    auto* camera = m_object->GetComponent(kCamera);
    int screen = ScreenMode();
    const char* arrayName = screen == 1   ? "Skater_Camera_2P_Vert_Array"
                            : screen == 2 ? "Skater_Camera_2P_Horiz_Array"
                                          : "Skater_Camera_Array";
    Script::CArray* modes = Script::GetArray(arrayName);
    Script::CStruct* params = Script::GetStructure(modes->GetElement(static_cast<uint32_t>(mode)));
    m_mode = mode;
    m_lipTrickTilt = 0.0f;
    float fov, behind, above, tilt, offset;
    params->GetFloat(kHorizFov, &fov, 0);
    params->GetFloat(kBehind, &behind, 0);
    params->GetFloat(kAbove, &above, 0);
    params->GetFloat(kTilt, &tilt, 0);
    params->GetFloat(kOriginOffset, &offset, 0);
    params->GetFloat(kLipTrickTilt, &m_lipTrickTilt, 0);
    params->GetFloat(kLipTrickAbove, &m_lipTrickAbove, 0);
    params->GetFloat(kSlerp, &m_slerp, 0);
    params->GetFloat(kVertAirSlerp, &m_vertAirSlerp, 0);
    params->GetFloat(kVertAirLandedSlerp, &m_vertAirLandedSlerp, 0);
    params->GetFloat(kLerpXZ, &m_lerpXZ, 0);
    params->GetFloat(kLerpY, &m_lerpY, 0);
    params->GetFloat(kVertAirLerpXZ, &m_vertAirLerpXZ, 0);
    params->GetFloat(kVertAirLerpY, &m_vertAirLerpY, 0);
    params->GetFloat(kLookRate, &m_lookRate, 0);
    params->GetFloat(kWallrideOffsetAngle, &m_wallrideOffsetAngle, 0);
    float zoomLerp;
    m_grindZoom = 1.0f;
    m_lipTrickZoom = 1.0f;
    m_bigAirTrickZoom = 1.0f;
    params->GetFloat(kZoomLerp, &zoomLerp, 0);
    params->GetFloat(kGrindZoom, &m_grindZoom, 0);
    params->GetFloat(kBigAirTrickZoom, &m_bigAirTrickZoom, 0);
    params->GetFloat(kLipTrickZoom, &m_lipTrickZoom, 0);
    m_zoomLerp = zoomLerp;
    const char* name;
    params->GetString(kName, &name, 0);
    m_originOffset = static_cast<float>(static_cast<double>(offset) * kFeet);
    m_horizFov = fov;
    m_tilt = tilt;
    double behindFeet = static_cast<double>(behind) * kFeet;
    if (time > 0.0f) {
        // lerped there a frame (1/60 s) at a time
        float behindTarget = static_cast<float>(behindFeet);
        float aboveTarget = static_cast<float>(static_cast<double>(above) * kFeet);
        m_behindTime = time;
        m_aboveTime = time;
        double perFrame = static_cast<double>(OneFrame()) / time;
        m_behindStep = static_cast<float>((static_cast<double>(behindTarget) - m_behind) * perFrame);
        m_aboveStep = static_cast<float>((static_cast<double>(aboveTarget) - m_above) * perFrame);
    } else {
        m_behind = static_cast<float>(behindFeet);
        m_behindTime = 0.0f;
        m_aboveTime = 0.0f;
        m_above = static_cast<float>(static_cast<double>(above) * kFeet);
    }
    // The camera component's camera (+0x18): its field of view (half across a vertical split).
    auto* view = *reinterpret_cast<Gfx::Camera**>(reinterpret_cast<uint8_t*>(camera) + 0x18);
    view->SetHFOV(screen == 1 ? static_cast<float>(static_cast<double>(fov) * kHalf) : fov);
    float heading;
    switch (mode) {
    case 5:
    case 6:
        heading = kPi;
        break;
    case 7:
    case 8:
        heading = kHalfPi;
        break;
    case 11:
    case 12:
        heading = -kHalfPi;
        break;
    default:
        heading = 0.0f;
        break;
    }
    mp_lookAround->m_modeHeading = heading;
}

} // namespace Obj

REPLACE(0x004F99F0, &Obj::CSkaterCameraComponent::DestroyBody);
REPLACE(0x004F9A20, &Obj::CSkaterCameraComponent::Finalize);
REPLACE(0x004F9A40, &Obj::CSkaterCameraComponent::SetState);
REPLACE(0x004F9970, &Obj::CSkaterCameraComponent::UpdateLerps);
REPLACE(0x004F9AE0, &Obj::CSkaterCameraComponent::SetMode);
