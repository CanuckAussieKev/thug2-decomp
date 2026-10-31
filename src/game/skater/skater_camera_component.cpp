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
#include "game/maths/mat4.h"
#include "game/maths/rotation_blend.h"
#include "game/maths/vec4.h"
#include "game/obj/camera_lookaround.h"
#include "game/obj/camera_util.h"
#include "game/obj/entity.h"
#include "game/obj/entity_hub.h"
#include "game/script/param_struct.h"
#include "game/skater/core_physics_fields.h"
#include "game/skater/rider.h"
#include "game/skater/walk_camera.h"
#include "game/sys/stopwatch.h"
#include "game/obj/compositemgr.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/skater/vehicle_sound.h"
#include "game/skater/skater_camera_component.h"

namespace GameObjects {

namespace {

using Skater::Physics::At;
using Skater::Physics::X87Sin;

constexpr uint32_t kSkaterCamera = 0x5E43A604;        // skatercamera (the component type)
extern void** const kCleanupVtable;
constexpr uint32_t kCleanupState = 0xD8CC3428;        // SkaterCleanupState (the component type)
constexpr uint32_t kSkaterState = 0x43686585;         // SkaterState (a component)
constexpr uint32_t kPhysicsControl = 0xD9151D4D;      // SkaterPhysicsControl (a component)
constexpr uint32_t kSkaterId = 0x4C48DA58;            // (ConfigureFromData's object id; name not recovered)
constexpr uint32_t kShake = 0x75A516D3;               // (the shake member function; name not recovered)
constexpr uint32_t kSetMode = 0xD7867FFE;             // (the camera mode member function; name not recovered)
constexpr uint32_t kMode = 0x6835B854;                // (its parameter; name not recovered)

// The shake's parameters (the game's strings).
const char* const kDuration = "duration";  // "duration"
const char* const kVertAmp = "vert_amp";   // "vert_amp"
const char* const kHorizAmp = "horiz_amp";  // "horiz_amp"
const char* const kVertVel = "vert_vel";   // "vert_vel"
const char* const kHorizVel = "horiz_vel"; // "horiz_vel"

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
bool CRiderChaseView::LookDown()
{
    if (!m_riderRef)
        return false;
    uint8_t* state = m_stateRef;
    if (At<int>(state, 0x1C))
        return false;
    if (At<int>(state, 0x18) == 3 || VertAir(state))
        return true;
    return state[0xF4] != 0;
}

// 0x004F9F00
void CRiderChaseView::ApplyShake(Maths::Vec4* position, const Maths::Mat4* orientation)
{
    double left = m_shakeLeft;
    double fraction = left / m_shakeLength;
    double across = X87Sin(m_shakeRateX * left * kTwoPi) * m_shakeAmpX * fraction;
    double up = X87Sin(m_shakeRateY * left * kTwoPi) * m_shakeAmpY * fraction;
    const Maths::Vec4& row0 = orientation->row[0];
    const Maths::Vec4& row1 = orientation->row[1];
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
    double now = m_shakeLeft - FrameClock::GetSlomoFrameSeconds();
    m_shakeLeft = static_cast<float>(now);
    if (now < 0.0)
        m_shakeLeft = 0.0f;
}

// 0x004FA020
void CRiderChaseView::FetchGoal(Maths::Vec4* out, bool instant)
{
    float seconds = static_cast<float>(FrameClock::GetSlomoFrameSeconds());
    CGameEntity* skater = m_riderRef;
    if (!skater) {
        *out = Maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    Maths::Vec4 position = skater->m_atPoint;
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
    uint8_t* state = m_stateRef;
    float across;
    double up;
    bool snap = instant;
    if (VertAir(state)) {
        across = static_cast<float>(
            FrameBlend(static_cast<float>(m_vertAirLerpXZ * static_cast<double>(m_rateScale)), seconds));
        up = FrameBlend(static_cast<float>(m_halfpipeUpBlend * static_cast<double>(m_rateScale)), seconds);
    } else {
        across = static_cast<float>(FrameBlend(static_cast<float>(m_lerpXZ * static_cast<double>(m_rateScale)), seconds));
        up = FrameBlend(static_cast<float>(m_lerpY * static_cast<double>(m_rateScale)), seconds);
        state = m_stateRef;
        if (state[0x98])
            up = 1.0;
        if (state[0xA0])
            snap = true;
    }
    Maths::Vec4& target = m_aimPoint;
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
void CRiderChaseView::GetZoomedOffsets(float* height, float* distance)
{
    if (!m_riderRef)
        return;
    m_zoomTarget = 1.0f;
    uint8_t* state = m_stateRef;
    if (!m_vertZoomOn && VertAir(state) && state[0xE0])
        m_vertZoomOn = 1;
    else if (!VertAir(m_stateRef))
        m_vertZoomOn = 0;
    state = m_stateRef;
    if (m_vertZoomOn)
        m_zoomTarget = m_bigAirTrickZoom;
    else if (At<int>(state, 0x18) == 4)
        m_zoomTarget = m_railZoomFactor;
    else if (At<int>(state, 0x18) == 3)
        m_zoomTarget = m_lipTrickZoom;
    CFreeLookModule* lookAround = m_freeLookModule;
    if (lookAround->m_turning && std::bit_cast<uint32_t>(lookAround->m_factor) != 0x3F800000u)
        m_zoomTarget = static_cast<float>(static_cast<double>(lookAround->m_factor) * m_zoomTarget);
    double eased = (static_cast<double>(m_zoomTarget) - m_zoom) * m_zoomEaseRate + m_zoom;
    m_zoom = static_cast<float>(eased);
    double behind = eased * m_behind;
    *distance = static_cast<float>(behind);
    float tilt = m_freeLookModule->m_leanAngle;
    if (tilt < 0.0f)
        *distance = static_cast<float>(((tilt + static_cast<double>(kTiltBias)) * kTiltScale * kTiltRange + kTiltBase) * behind);
    float above = At<int>(m_stateRef, 0x18) == 3 ? m_lipTrickAbove : m_above;
    if (m_zoom < 1.0f)
        *height = static_cast<float>((static_cast<double>(above) - kZoomedHeight) * m_zoom + kZoomedHeight);
    else
        *height = above;
}

// 0x004FA3C0
void CRiderChaseView::BindRider(CGameEntity* skater)
{
    m_riderRef = skater;
    if (!skater) {
        m_stateRef = nullptr;
        m_control = nullptr;
        return;
    }
    m_stateRef = reinterpret_cast<uint8_t*>(skater->FetchModule(kSkaterState));
    m_control = reinterpret_cast<uint8_t*>(m_riderRef->FetchModule(kPhysicsControl));
    m_instantFrames = 3;
}

// 0x004FA420
void CRiderChaseView::SaveState(WalkCameraState* state)
{
    std::memcpy(&state->orientation, m_orientation, sizeof(m_orientation));
    state->orientation.row[3] = Maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    state->target = m_aimPoint;
    state->turnCosine = m_turnCosine;
    state->zoom = m_zoom;
}

// 0x004FA4B0
CRiderChaseView* CRiderChaseView::MakeNew()
{
    CObjectModule::MakeNew();
    m_vtable = g_skaterCameraVtable;
    m_kindTag = kSkaterCamera;
    m_modeSel = 0;
    if (!MemSys::g_managerReady)
        MemSys::EnsureManager();
    void* memory = MemSys::Hub::Pull()->Carve(0x98, 1, nullptr);
    // (The interpolator's constructor does nothing: folded with many empty ones at 0x005902E0.)
    m_arcBlender = static_cast<Maths::ArcRotationBlender*>(memory);
    m_zoom = 1.0f;
    m_turnCosine = 1.0f;
    m_vertAirLerpXZ = 1.0f;
    m_halfpipeUpBlend = 1.0f;
    m_riderRef = nullptr;
    m_stateRef = nullptr;
    m_control = nullptr;
    m_zoomEaseRate = 0.0625f;
    m_lerpXZ = 0.25f;
    m_lerpY = 0.5f;
    m_lookRate = 0.1f;
    return this;
}

// 0x004FA590
CRiderChaseView* CRiderChaseView::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004FA5C0
void CRiderChaseView::ConfigureFromData(Scripts::CStruct* params)
{
    Maths::Mat4& matrix = m_entity->m_basisFrame;
    matrix.row[0] = Maths::Vec4(1.0f, 0.0f, 0.0f, 0.0f);
    matrix.row[1] = Maths::Vec4(0.0f, 1.0f, 0.0f, 0.0f);
    matrix.row[2] = Maths::Vec4(0.0f, 0.0f, 1.0f, 0.0f);
    matrix.row[3] = Maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    std::memcpy(m_orientation, &matrix, sizeof(m_orientation));
    uint32_t id = 0;
    params->ReadHashValue(kSkaterId, &id, 1);
    BindRider(static_cast<CGameEntity*>(reinterpret_cast<CObjectHubBase*>(g_compositeRegistry)->LookUpEntity(id)));
}

// 0x004FA680
int CRiderChaseView::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript*)
{
    if (name == kShake) {
        float duration = 0.0f, vertAmp = 0.0f, horizAmp = 0.0f, vertVel = 0.0f, horizVel = 0.0f;
        params->FetchFloat(kDuration, &duration, 0);
        params->FetchFloat(kVertAmp, &vertAmp, 0);
        params->FetchFloat(kHorizAmp, &horizAmp, 0);
        params->FetchFloat(kVertVel, &vertVel, 0);
        params->FetchFloat(kHorizVel, &horizVel, 0);
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
        params->FetchIntValue(kMode, &mode, 1);
        ChangeRegime(mode, 1.0f);
        return 1;
    }
    return 2;
}

// 0x004FA7C0
void CRiderChaseView::RestoreDefaultState() { ChangeRegime(m_modeSel, 1.0f); }

// 0x004FA7D0
void CRiderChaseView::NextMode()
{
    if (m_modeSel >= 5)
        return;
    if (++m_modeSel >= 5)
        m_modeSel = 1;
    ChangeRegime(m_modeSel, 1.0f);
}

// 0x004FA800
CObjectModule* __cdecl CRiderChaseView::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CRiderChaseView));
    if (!memory)
        return nullptr;
    return static_cast<CRiderChaseView*>(memory)->MakeNew();
}

// --- the cleanup state ---

// 0x004FBD90
void CRiderStateReset::DestroyBody()
{
    m_vtable = kCleanupVtable;
    CObjectModule::DestroyBody();
}

// 0x004FBDA0
void CRiderStateReset::ConfigureFromData(Scripts::CStruct*) { m_stateRef = nullptr; }

// 0x004FBDB0
CRiderStateReset* CRiderStateReset::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004FBDE0
void CRiderStateReset::Seal() { m_stateRef = m_entity->FetchModule(kSkaterState); }

// 0x004FBE00
void CRiderStateReset::TickFrame()
{
    CGameEntity* skater = m_entity;
    reinterpret_cast<CRider*>(skater)->UpdateShadowLight(reinterpret_cast<int>(&skater->m_atPoint),
                                                          reinterpret_cast<int>(&skater->m_basisFrame));
}

// 0x004FBE20
CObjectModule* __cdecl CRiderStateReset::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CRiderStateReset));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CRiderStateReset*>(memory);
    component->CObjectModule::MakeNew();
    component->m_vtable = kCleanupVtable;
    component->m_kindTag = kCleanupState;
    return component;
}

} // namespace Obj

using GameObjects::CRiderChaseView;
using GameObjects::CRiderStateReset;

namespace GameObjects {
namespace {

static VtableSlot kCleanupVtable_slots[] = {&CRiderStateReset::Teardown,
         &CRiderStateReset::TickFrame,
         &CRiderStateReset::ConfigureFromData,
         &CRiderStateReset::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kCleanupVtable = reinterpret_cast<void**>(kCleanupVtable_slots);

} // namespace
} // namespace Obj

namespace GameObjects {

static VtableSlot g_skaterCameraVtable_slots[] = {&CRiderChaseView::Teardown,
         &GameObjects::CRiderChaseView::TickFrame,
         &CRiderChaseView::ConfigureFromData,
         &GameObjects::CRiderChaseView::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &CRiderChaseView::InvokeMethodByName};
void** const g_skaterCameraVtable = reinterpret_cast<void**>(g_skaterCameraVtable_slots);

} // namespace Obj
