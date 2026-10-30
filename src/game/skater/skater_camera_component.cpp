// The skater camera component's life, parameters and small pieces (0x004F9EB0-0x004FA858), and the skater's
// cleanup-state component (0x004FBD90-0x004FBE76).
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include "game/skater/skater_camera_component.h"

#include <bit>
#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/matrix.h"
#include "game/mth/slerp.h"
#include "game/mth/vector.h"
#include "game/obj/camera_lookaround.h"
#include "game/obj/camera_util.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/script/struct.h"
#include "game/skater/core_physics_fields.h"
#include "game/skater/skater.h"
#include "game/skater/walk_camera.h"
#include "game/sys/timer.h"
#include "game/obj/compositemgr.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/game_code.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/skater/vehicle_sound.h"

namespace Obj {

namespace {

using Skater::Physics::At;
using Skater::Physics::X87Sin;

extern void** const kVtable;
constexpr uint32_t kSkaterCamera = 0x5E43A604;        // skatercamera (the component type)
extern void** const kCleanupVtable;
constexpr uint32_t kCleanupState = 0xD8CC3428;        // SkaterCleanupState (the component type)
constexpr uint32_t kSkaterState = 0x43686585;         // SkaterState (a component)
constexpr uint32_t kPhysicsControl = 0xD9151D4D;      // SkaterPhysicsControl (a component)
constexpr uint32_t kSkaterId = 0x4C48DA58;            // (InitFromStructure's object id; name not recovered)
constexpr uint32_t kShake = 0x75A516D3;               // (the shake member function; name not recovered)
constexpr uint32_t kSetMode = 0xD7867FFE;             // (the camera mode member function; name not recovered)
constexpr uint32_t kMode = 0x6835B854;                // (its parameter; name not recovered)

// The shake's parameters (the game's strings).
const char* const kDuration = GAME_TEXT(0x0064BB34, "duration");  // "duration"
const char* const kVertAmp = GAME_TEXT(0x0064BB28, "vert_amp");   // "vert_amp"
const char* const kHorizAmp = GAME_TEXT(0x0064BB1C, "horiz_amp");  // "horiz_amp"
const char* const kVertVel = GAME_TEXT(0x0064BB10, "vert_vel");   // "vert_vel"
const char* const kHorizVel = GAME_TEXT(0x0064BB04, "horiz_vel"); // "horiz_vel"

// The game's constants.
constexpr float kTwoPi = 6.2831854820251465f;   // 0x0064545C
constexpr float kTiltBias = 0.628f;             // 0x0064BB00
constexpr float kTiltScale = 1.5923566818237305f; // 0x0064BAFC
constexpr float kTiltRange = 0.6f;              // 0x006463C4
constexpr float kTiltBase = 0.4f;               // 0x0064BAF8
constexpr float kZoomedHeight = 3.0f;           // 0x006467A8

// In vert air: the state's +0x30 on, +0x48 and +0x78 off.
bool VertAir(const uint8_t* state) { return state[0x30] && !state[0x48] && !state[0x78]; }

} // namespace

// 0x004F9EB0
bool CSkaterCameraComponent::LookDown()
{
    if (!mp_skater)
        return false;
    uint8_t* state = mp_state;
    if (At<int>(state, 0x1C))
        return false;
    if (At<int>(state, 0x18) == 3 || VertAir(state))
        return true;
    return state[0xF4] != 0;
}

// 0x004F9F00
void CSkaterCameraComponent::ApplyShake(Mth::Vector* position, const Mth::Matrix* orientation)
{
    double left = m_shakeLeft;
    double fraction = left / m_shakeLength;
    double across = X87Sin(m_shakeRateX * left * kTwoPi) * m_shakeAmpX * fraction;
    double up = X87Sin(m_shakeRateY * left * kTwoPi) * m_shakeAmpY * fraction;
    const Mth::Vector& row0 = orientation->row[0];
    const Mth::Vector& row1 = orientation->row[1];
    float y0 = static_cast<float>(up * row0.y);
    float z0 = static_cast<float>(up * row0.z);
    float w0 = static_cast<float>(up * row0.w);
    float x = static_cast<float>(up * row0.x + position->x);
    position->x = x;
    float y = y0 + position->y;
    position->y = y;
    float z = z0 + position->z;
    position->z = z;
    float w = w0 + position->w;
    position->w = w;
    float x1 = static_cast<float>(across * row1.x);
    float y1 = static_cast<float>(across * row1.y);
    float z1 = static_cast<float>(across * row1.z);
    position->x = x + x1;
    position->y = y + y1;
    position->z = z + z1;
    position->w = static_cast<float>(w + across * row1.w);
    double now = m_shakeLeft - Tmr::GetSlomoFrameSeconds();
    m_shakeLeft = static_cast<float>(now);
    if (now < 0.0)
        m_shakeLeft = 0.0f;
}

// 0x004FA020
void CSkaterCameraComponent::GetTarget(Mth::Vector* out, bool instant)
{
    float seconds = static_cast<float>(Tmr::GetSlomoFrameSeconds());
    CCompositeObject* skater = mp_skater;
    if (!skater) {
        *out = Mth::Vector(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    Mth::Vector position = skater->m_pos;
    if (m_rateTime > 0.0f) {
        double left = m_rateTime - static_cast<double>(seconds);
        m_rateTime = static_cast<float>(left);
        if (left > 0.0) {
            m_rateScale = static_cast<float>(static_cast<double>(seconds) * m_rateRise + m_rateScale);
        } else {
            m_rateTime = 0.0f;
            m_rateScale = 1.0f;
        }
    } else {
        m_rateScale = 1.0f;
    }
    uint8_t* state = mp_state;
    float across;
    double up;
    bool snap = instant;
    if (VertAir(state)) {
        across = static_cast<float>(
            FrameBlend(static_cast<float>(m_vertAirLerpXZ * static_cast<double>(m_rateScale)), seconds));
        up = FrameBlend(static_cast<float>(m_vertAirLerpY * static_cast<double>(m_rateScale)), seconds);
    } else {
        across = static_cast<float>(FrameBlend(static_cast<float>(m_lerpXZ * static_cast<double>(m_rateScale)), seconds));
        up = FrameBlend(static_cast<float>(m_lerpY * static_cast<double>(m_rateScale)), seconds);
        state = mp_state;
        if (state[0x98])
            up = 1.0;
        if (state[0xA0])
            snap = true;
    }
    Mth::Vector& target = m_target;
    if (snap) {
        target = position;
    } else {
        float oldY = target.y;
        float oldZ = target.z;
        target.w = 1.0f;
        target.x = static_cast<float>((static_cast<double>(position.x) - target.x) * across + target.x);
        target.y = static_cast<float>((static_cast<double>(position.y) - oldY) * up + oldY);
        target.z = static_cast<float>((static_cast<double>(position.z) - oldZ) * across + oldZ);
    }
    *out = target;
}

// 0x004FA230
void CSkaterCameraComponent::GetZoomedOffsets(float* height, float* distance)
{
    if (!mp_skater)
        return;
    m_zoomTarget = 1.0f;
    uint8_t* state = mp_state;
    if (!m_vertZoomOn && VertAir(state) && state[0xE0])
        m_vertZoomOn = 1;
    else if (!VertAir(mp_state))
        m_vertZoomOn = 0;
    state = mp_state;
    if (m_vertZoomOn)
        m_zoomTarget = m_bigAirTrickZoom;
    else if (At<int>(state, 0x18) == 4)
        m_zoomTarget = m_grindZoom;
    else if (At<int>(state, 0x18) == 3)
        m_zoomTarget = m_lipTrickZoom;
    CCameraLookAroundComponent* lookAround = mp_lookAround;
    if (lookAround->m_turning && std::bit_cast<uint32_t>(lookAround->m_factor) != 0x3F800000u)
        m_zoomTarget = static_cast<float>(static_cast<double>(lookAround->m_factor) * m_zoomTarget);
    double eased = (static_cast<double>(m_zoomTarget) - m_zoom) * m_zoomLerp + m_zoom;
    m_zoom = static_cast<float>(eased);
    double behind = eased * m_behind;
    *distance = static_cast<float>(behind);
    float tilt = mp_lookAround->m_tilt;
    if (tilt < 0.0f)
        *distance = static_cast<float>(((tilt + static_cast<double>(kTiltBias)) * kTiltScale * kTiltRange + kTiltBase) * behind);
    float above = At<int>(mp_state, 0x18) == 3 ? m_lipTrickAbove : m_above;
    if (m_zoom < 1.0f)
        *height = static_cast<float>((static_cast<double>(above) - kZoomedHeight) * m_zoom + kZoomedHeight);
    else
        *height = above;
}

// 0x004FA3C0
void CSkaterCameraComponent::SetSkater(CCompositeObject* skater)
{
    mp_skater = skater;
    if (!skater) {
        mp_state = nullptr;
        mp_control = nullptr;
        return;
    }
    mp_state = reinterpret_cast<uint8_t*>(skater->GetComponent(kSkaterState));
    mp_control = reinterpret_cast<uint8_t*>(mp_skater->GetComponent(kPhysicsControl));
    m_instantFrames = 3;
}

// 0x004FA420
void CSkaterCameraComponent::SaveState(WalkCameraState* state)
{
    std::memcpy(&state->orientation, m_orientation, sizeof(m_orientation));
    state->orientation.row[3] = Mth::Vector(0.0f, 0.0f, 0.0f, 1.0f);
    state->target = m_target;
    state->turnCosine = m_turnCosine;
    state->zoom = m_zoom;
}

// 0x004FA4B0
CSkaterCameraComponent* CSkaterCameraComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = kVtable;
    m_type = kSkaterCamera;
    m_mode = 0;
    if (!Mem::g_managerReady)
        Mem::EnsureManager();
    void* memory = Mem::Manager::Get()->Allocate(0x98, 1, nullptr);
    // (The interpolator's constructor does nothing: folded with many empty ones at 0x005902E0.)
    mp_slerp = static_cast<Mth::SlerpInterpolator*>(
        memory ? GameFunction<void*(__thiscall*)(void*)>(0x005902E0)(memory) : nullptr);
    m_zoom = 1.0f;
    m_turnCosine = 1.0f;
    m_vertAirLerpXZ = 1.0f;
    m_vertAirLerpY = 1.0f;
    mp_skater = nullptr;
    mp_state = nullptr;
    mp_control = nullptr;
    m_zoomLerp = 0.0625f;
    m_lerpXZ = 0.25f;
    m_lerpY = 0.5f;
    m_lookRate = 0.1f;
    return this;
}

// 0x004FA590
CSkaterCameraComponent* CSkaterCameraComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004FA5C0
void CSkaterCameraComponent::InitFromStructure(Script::CStruct* params)
{
    Mth::Matrix& matrix = m_object->m_matrix;
    matrix.row[0] = Mth::Vector(1.0f, 0.0f, 0.0f, 0.0f);
    matrix.row[1] = Mth::Vector(0.0f, 1.0f, 0.0f, 0.0f);
    matrix.row[2] = Mth::Vector(0.0f, 0.0f, 1.0f, 0.0f);
    matrix.row[3] = Mth::Vector(0.0f, 0.0f, 0.0f, 1.0f);
    std::memcpy(m_orientation, &matrix, sizeof(m_orientation));
    uint32_t id = 0;
    params->GetChecksum(kSkaterId, &id, 1);
    SetSkater(static_cast<CCompositeObject*>(reinterpret_cast<CBaseManager*>(g_compositeObjectManager)->GetObjectByID(id)));
}

// 0x004FA680
int CSkaterCameraComponent::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript*)
{
    if (name == kShake) {
        float duration = 0.0f, vertAmp = 0.0f, horizAmp = 0.0f, vertVel = 0.0f, horizVel = 0.0f;
        params->GetFloat(kDuration, &duration, 0);
        params->GetFloat(kVertAmp, &vertAmp, 0);
        params->GetFloat(kHorizAmp, &horizAmp, 0);
        params->GetFloat(kVertVel, &vertVel, 0);
        params->GetFloat(kHorizVel, &horizVel, 0);
        m_shakeAmpX = vertAmp;
        m_shakeLeft = duration;
        m_shakeLength = duration;
        m_shakeRateY = horizVel;
        m_shakeAmpY = horizAmp;
        m_shakeRateX = vertVel;
        return 1;
    }
    if (name == kSetMode) {
        int mode = 0;
        params->GetInteger(kMode, &mode, 1);
        SetMode(mode, 1.0f);
        return 1;
    }
    return 2;
}

// 0x004FA7C0
void CSkaterCameraComponent::ResetMode() { SetMode(m_mode, 1.0f); }

// 0x004FA7D0
void CSkaterCameraComponent::NextMode()
{
    if (m_mode >= 5)
        return;
    if (++m_mode >= 5)
        m_mode = 1;
    SetMode(m_mode, 1.0f);
}

// 0x004FA800
CBaseComponent* __cdecl CSkaterCameraComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CSkaterCameraComponent));
    if (!memory)
        return nullptr;
    return static_cast<CSkaterCameraComponent*>(memory)->Construct();
}

// --- the cleanup state ---

// 0x004FBD90
void CSkaterCleanupStateComponent::DestroyBody()
{
    m_vtable = kCleanupVtable;
    CBaseComponent::DestroyBody();
}

// 0x004FBDA0
void CSkaterCleanupStateComponent::InitFromStructure(Script::CStruct*) { mp_state = nullptr; }

// 0x004FBDB0
CSkaterCleanupStateComponent* CSkaterCleanupStateComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004FBDE0
void CSkaterCleanupStateComponent::Finalize() { mp_state = m_object->GetComponent(kSkaterState); }

// 0x004FBE00
void CSkaterCleanupStateComponent::Update()
{
    CCompositeObject* skater = m_object;
    reinterpret_cast<CSkater*>(skater)->UpdateShadowLight(reinterpret_cast<int>(&skater->m_pos),
                                                          reinterpret_cast<int>(&skater->m_matrix));
}

// 0x004FBE20
CBaseComponent* __cdecl CSkaterCleanupStateComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CSkaterCleanupStateComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CSkaterCleanupStateComponent*>(memory);
    component->CBaseComponent::Construct();
    component->m_vtable = kCleanupVtable;
    component->m_type = kCleanupState;
    return component;
}

} // namespace Obj

using Obj::CSkaterCameraComponent;
using Obj::CSkaterCleanupStateComponent;
REPLACE(0x004F9EB0, &CSkaterCameraComponent::LookDown);
REPLACE(0x004F9F00, &CSkaterCameraComponent::ApplyShake);
REPLACE(0x004FA020, &CSkaterCameraComponent::GetTarget);
REPLACE(0x004FA230, &CSkaterCameraComponent::GetZoomedOffsets);
REPLACE(0x004FA3C0, &CSkaterCameraComponent::SetSkater);
REPLACE(0x004FA420, &CSkaterCameraComponent::SaveState);
REPLACE(0x004FA4B0, &CSkaterCameraComponent::Construct);
REPLACE(0x004FA590, &CSkaterCameraComponent::Destroy);
REPLACE(0x004FA5C0, &CSkaterCameraComponent::InitFromStructure);
REPLACE(0x004FA680, &CSkaterCameraComponent::CallMemberFunction);
REPLACE(0x004FA7C0, &CSkaterCameraComponent::ResetMode);
REPLACE(0x004FA7D0, &CSkaterCameraComponent::NextMode);
REPLACE(0x004FA800, CSkaterCameraComponent::Create);
REPLACE(0x004FBD90, &CSkaterCleanupStateComponent::DestroyBody);
REPLACE(0x004FBDA0, &CSkaterCleanupStateComponent::InitFromStructure);
REPLACE(0x004FBDB0, &CSkaterCleanupStateComponent::Destroy);
REPLACE(0x004FBDE0, &CSkaterCleanupStateComponent::Finalize);
REPLACE(0x004FBE00, &CSkaterCleanupStateComponent::Update);
REPLACE(0x004FBE20, CSkaterCleanupStateComponent::Create);

namespace Obj {
namespace {

GAME_VTABLE(kVtable, 0x0064BA74,
    &CSkaterCameraComponent::Destroy,
    &Obj::CSkaterCameraComponent::Update,
    &CSkaterCameraComponent::InitFromStructure,
    &Obj::CSkaterCameraComponent::Finalize,
    Stubs::DoNothing,
    Obj::RefreshFromStructure,
    Stubs::IgnoreArguments2,
    &Obj::CBaseComponent::Suspend,
    Stubs::IgnoreArgument,
    Stubs::DoNothing,
    Stubs::IgnoreArgument,
    static_cast<int (Obj::CBaseComponent::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CBaseComponent::CallMemberFunction),
    &CSkaterCameraComponent::CallMemberFunction);
GAME_VTABLE(kCleanupVtable, 0x0064BB48,
    &CSkaterCleanupStateComponent::Destroy,
    &CSkaterCleanupStateComponent::Update,
    &CSkaterCleanupStateComponent::InitFromStructure,
    &CSkaterCleanupStateComponent::Finalize,
    Stubs::DoNothing,
    Obj::RefreshFromStructure,
    Stubs::IgnoreArguments2,
    &Obj::CBaseComponent::Suspend,
    Stubs::IgnoreArgument,
    Stubs::DoNothing,
    Stubs::IgnoreArgument,
    static_cast<int (Obj::CBaseComponent::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CBaseComponent::CallMemberFunction),
    &Obj::ComponentWithoutMemberFunctions::CallMemberFunction);

} // namespace
} // namespace Obj
