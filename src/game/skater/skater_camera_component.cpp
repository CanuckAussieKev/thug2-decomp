// The skater camera component's life, parameters and small pieces (0x004F9F00-0x004FA858), and the skater's
// cleanup-state component (0x004FBD90-0x004FBE76).
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include "game/skater/skater_camera_component.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/camera_util.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/script/struct.h"
#include "game/skater/core_physics_fields.h"
#include "game/skater/skater.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

using Skater::Physics::At;
using Skater::Physics::X87Sin;

constexpr uint32_t kVtable = 0x0064BA74;
constexpr uint32_t kSkaterCamera = 0x5E43A604;        // skatercamera (the component type)
constexpr uint32_t kCleanupVtable = 0x0064BB48;
constexpr uint32_t kCleanupState = 0xD8CC3428;        // SkaterCleanupState (the component type)
constexpr uint32_t kSkaterState = 0x43686585;         // SkaterState (a component)
constexpr uint32_t kPhysicsControl = 0xD9151D4D;      // SkaterPhysicsControl (a component)
constexpr uint32_t kSkaterId = 0x4C48DA58;            // (InitFromStructure's object id; name not recovered)
constexpr uint32_t kShake = 0x75A516D3;               // (the shake member function; name not recovered)
constexpr uint32_t kSetMode = 0xD7867FFE;             // (the camera mode member function; name not recovered)
constexpr uint32_t kMode = 0x6835B854;                // (its parameter; name not recovered)

// The shake's parameters (the game's strings).
const char* const kDuration = reinterpret_cast<const char*>(0x0064BB34);  // "duration"
const char* const kVertAmp = reinterpret_cast<const char*>(0x0064BB28);   // "vert_amp"
const char* const kHorizAmp = reinterpret_cast<const char*>(0x0064BB1C);  // "horiz_amp"
const char* const kVertVel = reinterpret_cast<const char*>(0x0064BB10);   // "vert_vel"
const char* const kHorizVel = reinterpret_cast<const char*>(0x0064BB04); // "horiz_vel"

// The component's fields.
constexpr uint32_t kObject = 0x0C;
constexpr uint32_t kMode_ = 0x24;               // the camera mode (1-4)
constexpr uint32_t kInstantFrames = 0x28;       // frames that jump straight to the target
constexpr uint32_t kTurnCosine = 0x2C;        // the last turn's cosine (the new facing against the old)
constexpr uint32_t kTarget = 0x30;              // the point followed
constexpr uint32_t kOrientation = 0x40;         // (three rows)
constexpr uint32_t kHeight = 0xD0;
constexpr uint32_t kDistance = 0xC4;
constexpr uint32_t kRateAcross = 0xE4;
constexpr uint32_t kRateUp = 0xE8;
constexpr uint32_t kVertRateAcross = 0xEC;
constexpr uint32_t kVertRateUp = 0xF0;
constexpr uint32_t kLookRate = 0xF4;
constexpr uint32_t kRateTime = 0xF8;            // seconds left of the rates' rise back to 1
constexpr uint32_t kRateRise = 0xFC;
constexpr uint32_t kRateScale = 0x100;
constexpr uint32_t kVertZoomOn = 0x104;
constexpr uint32_t kCurrentZoom = 0x108;
constexpr uint32_t kZoomTarget = 0x10C;
constexpr uint32_t kZoomRate = 0x110;
constexpr uint32_t kVertZoom = 0x114;
constexpr uint32_t kRailZoom = 0x118;
constexpr uint32_t kLipZoom = 0x11C;
constexpr uint32_t kLipHeight = 0x138;
constexpr uint32_t kShakeLeft = 0x150;
constexpr uint32_t kShakeLength = 0x154;
constexpr uint32_t kShakeAmpX = 0x158;
constexpr uint32_t kShakeAmpY = 0x15C;
constexpr uint32_t kShakeRateX = 0x160;
constexpr uint32_t kShakeRateY = 0x164;
constexpr uint32_t kLookAround = 0x168;         // the CameraLookAround component (+0x19 on, +0x1C zoom, +0x20 tilt)
constexpr uint32_t kInterpolator = 0x16C;       // Mth::SlerpInterpolator (0x98 bytes)
constexpr uint32_t kSkater = 0x170;
constexpr uint32_t kState = 0x174;              // the skater's SkaterState (+0x18 the state, flags as bytes)
constexpr uint32_t kControl = 0x178;            // the skater's SkaterPhysicsControl

// The game's constants.
constexpr float kTwoPi = 6.2831854820251465f;   // 0x0064545C
constexpr float kTiltBias = 0.628f;             // 0x0064BB00
constexpr float kTiltScale = 1.5923566818237305f; // 0x0064BAFC
constexpr float kTiltRange = 0.6f;              // 0x006463C4
constexpr float kTiltBase = 0.4f;               // 0x0064BAF8
constexpr float kZoomedHeight = 3.0f;           // 0x006467A8

uint8_t* Bytes(void* p) { return static_cast<uint8_t*>(p); }
Mth::Vector& VectorAt(void* p, uint32_t offset) { return At<Mth::Vector>(p, offset); }
uint8_t* StateOf(void* camera) { return At<uint8_t*>(camera, kState); }
// In vert air: the state's +0x30 on, +0x48 and +0x78 off.
bool VertAir(const uint8_t* state) { return state[0x30] && !state[0x48] && !state[0x78]; }
// 0x004F9AE0 (thiscall, RET 8; not rewritten here): the camera mode `mode` set, blended in over `blend`.
void SetMode(void* camera, int mode, float blend)
{
    GameFunction<void(__thiscall*)(void*, int, float)>(0x004F9AE0)(camera, mode, blend);
}

} // namespace

// 0x004F9EB0
bool CSkaterCameraComponent::LookDown()
{
    if (!At<void*>(this, kSkater))
        return false;
    uint8_t* state = StateOf(this);
    if (At<int>(state, 0x1C))
        return false;
    if (At<int>(state, 0x18) == 3 || VertAir(state))
        return true;
    return state[0xF4] != 0;
}

// 0x004F9F00
void CSkaterCameraComponent::ApplyShake(Mth::Vector* position, const Mth::Matrix* orientation)
{
    double left = At<float>(this, kShakeLeft);
    double fraction = left / At<float>(this, kShakeLength);
    double across = X87Sin(At<float>(this, kShakeRateX) * left * kTwoPi) * At<float>(this, kShakeAmpX) * fraction;
    double up = X87Sin(At<float>(this, kShakeRateY) * left * kTwoPi) * At<float>(this, kShakeAmpY) * fraction;
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
    double now = At<float>(this, kShakeLeft) - Tmr::GetSlomoFrameSeconds();
    At<float>(this, kShakeLeft) = static_cast<float>(now);
    if (now < 0.0)
        At<float>(this, kShakeLeft) = 0.0f;
}

// 0x004FA020
void CSkaterCameraComponent::GetTarget(Mth::Vector* out, bool instant)
{
    float seconds = static_cast<float>(Tmr::GetSlomoFrameSeconds());
    auto* skater = At<CCompositeObject*>(this, kSkater);
    if (!skater) {
        *out = Mth::Vector(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    Mth::Vector position = skater->m_pos;
    float& time = At<float>(this, kRateTime);
    if (time > 0.0f) {
        double left = time - static_cast<double>(seconds);
        time = static_cast<float>(left);
        if (left > 0.0) {
            At<float>(this, kRateScale) =
                static_cast<float>(static_cast<double>(seconds) * At<float>(this, kRateRise) + At<float>(this, kRateScale));
        } else {
            time = 0.0f;
            At<float>(this, kRateScale) = 1.0f;
        }
    } else {
        At<float>(this, kRateScale) = 1.0f;
    }
    uint8_t* state = StateOf(this);
    float across;
    double up;
    bool snap = instant;
    if (VertAir(state)) {
        across = static_cast<float>(FrameBlend(static_cast<float>(At<float>(this, kVertRateAcross) *
                                                                  static_cast<double>(At<float>(this, kRateScale))),
                                               seconds));
        up = FrameBlend(static_cast<float>(At<float>(this, kVertRateUp) * static_cast<double>(At<float>(this, kRateScale))),
                        seconds);
    } else {
        across = static_cast<float>(FrameBlend(
            static_cast<float>(At<float>(this, kRateAcross) * static_cast<double>(At<float>(this, kRateScale))), seconds));
        up = FrameBlend(static_cast<float>(At<float>(this, kRateUp) * static_cast<double>(At<float>(this, kRateScale))),
                        seconds);
        state = StateOf(this);
        if (state[0x98])
            up = 1.0;
        if (state[0xA0])
            snap = true;
    }
    Mth::Vector& target = VectorAt(this, kTarget);
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
    if (!At<void*>(this, kSkater))
        return;
    float& zoomTarget = At<float>(this, kZoomTarget);
    zoomTarget = 1.0f;
    uint8_t* state = StateOf(this);
    uint8_t& vertZoom = At<uint8_t>(this, kVertZoomOn);
    if (!vertZoom && VertAir(state) && state[0xE0])
        vertZoom = 1;
    else if (!VertAir(StateOf(this)))
        vertZoom = 0;
    state = StateOf(this);
    if (vertZoom)
        zoomTarget = At<float>(this, kVertZoom);
    else if (At<int>(state, 0x18) == 4)
        zoomTarget = At<float>(this, kRailZoom);
    else if (At<int>(state, 0x18) == 3)
        zoomTarget = At<float>(this, kLipZoom);
    uint8_t* lookAround = At<uint8_t*>(this, kLookAround);
    if (lookAround[0x19] && At<uint32_t>(lookAround, 0x1C) != 0x3F800000u)
        zoomTarget = static_cast<float>(static_cast<double>(At<float>(lookAround, 0x1C)) * zoomTarget);
    float& zoom = At<float>(this, kCurrentZoom);
    double eased = (static_cast<double>(zoomTarget) - zoom) * At<float>(this, kZoomRate) + zoom;
    zoom = static_cast<float>(eased);
    double behind = eased * At<float>(this, kDistance);
    *distance = static_cast<float>(behind);
    lookAround = At<uint8_t*>(this, kLookAround);
    float tilt = At<float>(lookAround, 0x20);
    if (tilt < 0.0f)
        *distance = static_cast<float>(((tilt + static_cast<double>(kTiltBias)) * kTiltScale * kTiltRange + kTiltBase) * behind);
    float above = At<int>(StateOf(this), 0x18) == 3 ? At<float>(this, kLipHeight) : At<float>(this, kHeight);
    if (At<float>(this, kCurrentZoom) < 1.0f)
        *height = static_cast<float>((static_cast<double>(above) - kZoomedHeight) * At<float>(this, kCurrentZoom) +
                                     kZoomedHeight);
    else
        *height = above;
}

// 0x004FA3C0
void CSkaterCameraComponent::SetSkater(CCompositeObject* skater)
{
    At<CCompositeObject*>(this, kSkater) = skater;
    if (!skater) {
        At<void*>(this, kState) = nullptr;
        At<void*>(this, kControl) = nullptr;
        return;
    }
    At<void*>(this, kState) = skater->GetComponent(kSkaterState);
    At<void*>(this, kControl) = At<CCompositeObject*>(this, kSkater)->GetComponent(kPhysicsControl);
    At<int>(this, kInstantFrames) = 3;
}

// 0x004FA420
void CSkaterCameraComponent::SaveState(void* state)
{
    for (uint32_t i = 0; i < 0x30; i += 4)
        At<uint32_t>(state, i) = At<uint32_t>(this, kOrientation + i);
    VectorAt(state, 0x30) = Mth::Vector(0.0f, 0.0f, 0.0f, 1.0f);
    VectorAt(state, 0x40) = VectorAt(this, kTarget);
    At<float>(state, 0x50) = At<float>(this, kTurnCosine);
    At<float>(state, 0x54) = At<float>(this, kCurrentZoom);
}

// 0x004FA4B0
CSkaterCameraComponent* CSkaterCameraComponent::Construct()
{
    auto* base = reinterpret_cast<CBaseComponent*>(this);
    base->Construct();
    At<uint32_t>(this, 0) = kVtable;
    At<uint32_t>(this, 4) = kSkaterCamera;
    At<int>(this, kMode_) = 0;
    if (!GameVariable<bool>(0x007CC979))
        Mem::EnsureManager();
    void* memory = Mem::Manager::Get()->Allocate(0x98, 1, nullptr);
    // (The interpolator's constructor does nothing: folded with many empty ones at 0x005902E0.)
    At<void*>(this, kInterpolator) =
        memory ? GameFunction<void*(__thiscall*)(void*)>(0x005902E0)(memory) : nullptr;
    At<float>(this, kCurrentZoom) = 1.0f;
    At<float>(this, kTurnCosine) = 1.0f;
    At<float>(this, kVertRateAcross) = 1.0f;
    At<float>(this, kVertRateUp) = 1.0f;
    At<void*>(this, kSkater) = nullptr;
    At<void*>(this, kState) = nullptr;
    At<void*>(this, kControl) = nullptr;
    At<float>(this, kZoomRate) = 0.0625f;
    At<float>(this, kRateAcross) = 0.25f;
    At<float>(this, kRateUp) = 0.5f;
    At<float>(this, kLookRate) = 0.1f;
    return this;
}

// 0x004FA590
CSkaterCameraComponent* CSkaterCameraComponent::Destroy(unsigned int flags)
{
    GameFunction<void(__thiscall*)(void*)>(0x004F99F0)(this); // (the destructor body)
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004FA5C0
void CSkaterCameraComponent::InitFromStructure(Script::CStruct* params)
{
    Mth::Matrix& matrix = At<CCompositeObject*>(this, kObject)->m_matrix;
    matrix.row[0] = Mth::Vector(1.0f, 0.0f, 0.0f, 0.0f);
    matrix.row[1] = Mth::Vector(0.0f, 1.0f, 0.0f, 0.0f);
    matrix.row[2] = Mth::Vector(0.0f, 0.0f, 1.0f, 0.0f);
    matrix.row[3] = Mth::Vector(0.0f, 0.0f, 0.0f, 1.0f);
    for (uint32_t i = 0; i < 0x30; i += 4)
        At<uint32_t>(this, kOrientation + i) = At<uint32_t>(&matrix, i);
    uint32_t id = 0;
    params->GetChecksum(kSkaterId, &id, 1);
    SetSkater(static_cast<CCompositeObject*>(GameVariable<CBaseManager*>(0x006F0FB8)->GetObjectByID(id)));
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
        At<float>(this, kShakeAmpX) = vertAmp;
        At<float>(this, kShakeLeft) = duration;
        At<float>(this, kShakeLength) = duration;
        At<float>(this, kShakeRateY) = horizVel;
        At<float>(this, kShakeAmpY) = horizAmp;
        At<float>(this, kShakeRateX) = vertVel;
        return 1;
    }
    if (name == kSetMode) {
        int mode = 0;
        params->GetInteger(kMode, &mode, 1);
        SetMode(this, mode, 1.0f);
        return 1;
    }
    return 2;
}

// 0x004FA7C0
void CSkaterCameraComponent::ResetMode() { SetMode(this, At<int>(this, kMode_), 1.0f); }

// 0x004FA7D0
void CSkaterCameraComponent::NextMode()
{
    int& mode = At<int>(this, kMode_);
    if (mode >= 5)
        return;
    if (++mode >= 5)
        mode = 1;
    SetMode(this, mode, 1.0f);
}

// 0x004FA800
CBaseComponent* __cdecl CSkaterCameraComponent::Create()
{
    void* memory = Mem::Calloc(0x17C);
    if (!memory)
        return nullptr;
    return reinterpret_cast<CBaseComponent*>(static_cast<CSkaterCameraComponent*>(memory)->Construct());
}

// --- the cleanup state ---

// 0x004FBD90
void CSkaterCleanupStateComponent::DestroyBody()
{
    At<uint32_t>(this, 0) = kCleanupVtable;
    reinterpret_cast<CBaseComponent*>(this)->DestroyBody();
}

// 0x004FBDA0
void CSkaterCleanupStateComponent::InitFromStructure(Script::CStruct*) { At<void*>(this, 0x18) = nullptr; }

// 0x004FBDB0
CSkaterCleanupStateComponent* CSkaterCleanupStateComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004FBDE0
void CSkaterCleanupStateComponent::Finalize()
{
    At<void*>(this, 0x18) = At<CCompositeObject*>(this, kObject)->GetComponent(kSkaterState);
}

// 0x004FBE00
void CSkaterCleanupStateComponent::Update()
{
    auto* skater = At<CCompositeObject*>(this, kObject);
    reinterpret_cast<CSkater*>(skater)->UpdateShadowLight(reinterpret_cast<int>(&skater->m_pos),
                                                          reinterpret_cast<int>(&skater->m_matrix));
}

// 0x004FBE20
CBaseComponent* __cdecl CSkaterCleanupStateComponent::Create()
{
    void* memory = Mem::Calloc(0x1C);
    if (!memory)
        return nullptr;
    auto* component = static_cast<CBaseComponent*>(memory);
    component->Construct();
    At<uint32_t>(component, 0) = kCleanupVtable;
    At<uint32_t>(component, 4) = kCleanupState;
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
