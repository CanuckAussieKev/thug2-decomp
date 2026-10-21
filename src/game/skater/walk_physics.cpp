// Pieces of the Walk component (Obj::CWalkComponent): see walk_physics.h. From the disassembly of 0x00524E10-
// 0x00529F62 (0x00525E40, 0x00525E90, 0x00525EA0, 0x00525F60, 0x00526060, 0x00528580-0x00528740 and 0x00528890
// aren't Ghidra functions).

#include "game/skater/walk_physics.h"

#include <cstdint>
#include <cstring>

#include "game/nx/coll_cache.h"
#include "game/nx/feeler.h"
#include "game/obj/anim_blender.h"
#include "game/obj/compositemgr.h"
#include "game/obj/rail_manager.h"
#include "game/obj/smtptr.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skater/x87_math.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

template <typename T>
T& At(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

constexpr uint32_t kWalk = 0x726E85AA;
void** const kVtable = reinterpret_cast<void**>(0x0064C75C);

// The fields (offsets into the component).
constexpr uint32_t kState = 0x18, kStateTime = 0x1C, kPreviousState = 0x20;
constexpr uint32_t kFacing = 0x48;           // Mth::Vector
constexpr uint32_t kAnimState = 0x78;        // a name: Run, Stand, Skid, Land, AcidDrop, rail, Wallplant...
constexpr uint32_t kOnRail = 0x9B;           // bool
constexpr uint32_t kContacts = 0xA0;         // 8 x 0xCC
constexpr uint32_t kAnimSpeedScaling = 0x700; // 0 off, 1 Run, 2 HangMove, 3 LadderMove
constexpr uint32_t kSpeed = 0x704, kLastSpeed = 0x708;
constexpr uint32_t kForwardControlLock = 0x70C; // bool
constexpr uint32_t kFrames710 = 0x710;
constexpr uint32_t kCollCache = 0x790;
constexpr uint32_t kCamera = 0x175C, kWalkCamera = 0x1760;
constexpr uint32_t kHangNode = 0x1764, kHangNextNode = 0x1768, kRailManager = 0x176C;
constexpr uint32_t kInput = 0x1770, kAnimBlender = 0x1774, kModel = 0x1778, kTrigger = 0x177C,
                   kPhysicsControl = 0x1780, kMovableContact = 0x1784, kSkaterState = 0x1788,
                   kCorePhysics = 0x178C, kTaggingControl = 0x1790;
constexpr uint32_t kRunning = 0x17A4, kFlag17A8 = 0x17A8;
constexpr uint32_t kStandardSpeed = 0x17B0;
constexpr uint32_t kObjectRef = 0x184C;      // CSmtPtr
constexpr uint32_t kRunAdjustment = 0x1868;
constexpr uint32_t kFlag186C = 0x186C, kHeight1870 = 0x1870, kFlag1874 = 0x1874;
constexpr uint32_t kJumpTime = 0x1898, kJumpVelocity = 0x189C;
constexpr uint32_t kPushing = 0x18B0;
constexpr uint32_t kHangInitAnimType = 0x18D8, kClimbScale = 0x18DC, kLadderDirection = 0x18E0;
constexpr uint32_t kClimbOffset = 0x1934;

// Script names.
constexpr uint32_t kWalkParameters = 0x6775E538, kHangControlDebounceTime = 0xAC96D24B,
                   kFeelerHeight = 0x6DA7F696, kStandPosSearchDepth = 0xAEBFA9CD, kRunAdjustRate = 0x3FB31DFC,
                   kGravity = 0xA5E2DA58, kCheatMoon = 0x9C8C6DF1, kMaxSlowWalkSpeed = 0xF3649996,
                   kMaxFastWalkSpeed = 0x6A5805D8, kMaxSlowRunSpeed = 0x1C94CC9C;
constexpr uint32_t kOff = 0xD443A2BC, kRun = 0xAF895B3F, kHangMove = 0x6384F1DA, kLadderMove = 0xA2BFE505,
                   kStandardSpeedName = 0xB2D59BAF, kCameraName = 0xC4E311FA, kWalkCameraType = 0xC7D37EC2,
                   kStateTimeName = 0x5AB23CC9, kSpeedScale = 0x092C388F, kWalkSlow = 0x1150CABB,
                   kWalkFast = 0x0131F2A2, kRunSlow = 0x5606D106, kRunFast = 0x4667E91F,
                   kHangInitAnimTypeName = 0x85FA9AC4, kStateDuration = 0x4B88E4E4, kPreviousStateName = 0xF78635DA,
                   kStateName = 0x5C6C2D04, kHangAngle = 0xEAD7D286, kOn = 0xF649D637, kSkid = 0x1D537EFF,
                   kStand = 0x9B46E749, kSkaterScore = 0xD262802F;

constexpr float kHangDebounceScale = -1000.0f; // 0x0064604C
constexpr float kSeventhTurn = 0.8975979089736938f; // 0x00646968 (2 pi / 7)
constexpr float kDegrees = 57.2957763671875f;  // 0x00645468
constexpr float kMs = 0.001f;                  // 0x006461D8 (0.0010000000474974513f)
constexpr float kHalf = 0.5f;                  // 0x006453BC

// The state names (0x0064C618: WALKING_GROUND, WALKING_AIR, ...).
uint32_t StateName(int state) { return reinterpret_cast<const uint32_t*>(0x0064C618)[state]; }

// The original's _ftol (0x006236FC): truncated toward zero.
int Truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// A float of walk_parameters (asserted).
float WalkParameter(uint32_t name)
{
    float value;
    Script::GetStructure(kWalkParameters)->GetFloat(name, &value, 1);
    return value;
}

// The game clock with its offset (0x00696D60), and the time since `then` on it.
int ClockSince(int then) { return Tmr::GameClockElapsed() + (GameVariable<int>(0x00696D60) - then); }

// The skate module's career (0x007CE478's +0x20).
CSkaterCareer* Career() { return At<CSkaterCareer*>(GameVariable<void*>(0x007CE478), 0x20); }

// asin (the game's C runtime) of `value` in degrees (x 57.29578), negated when `negate` - the result kept in the
// register until it is rounded to float, as the original.
float AsinDegrees(double value, bool negate)
{
    float result;
    uint32_t function = 0x00623560;
    float degrees = kDegrees;
    __asm {
        fld value
        call function
        cmp negate, 0
        je positive
        fchs
    positive:
        fmul degrees
        fstp result
    }
    return result;
}

// GetTurnedFacing's turn (0x00528220): the cosine rounded to float, the sine kept in the register.
void TurnFlat(double angle, float x, float z, float* turnedX, float* turnedZ)
{
    float cosine, rx, rz;
    __asm {
        fld angle
        fld st(0)
        fcos
        fstp cosine
        fsin                // sin
        fld cosine
        fmul x              // c*x, sin
        fld st(1)
        fmul z              // sin*z, c*x, sin
        fsubp st(1), st     // c*x - sin*z, sin
        fstp rx             // sin
        fmul x              // sin*x
        fld cosine
        fmul z              // c*z, sin*x
        faddp st(1), st
        fstp rz
    }
    *turnedX = rx;
    *turnedZ = rz;
}

} // namespace

// 0x00525E40
void __cdecl CWalkComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kWalk), reinterpret_cast<const uint32_t*>(0x006847F8),
                                        0x18);
}

// 0x00528880
WalkContact* WalkContact::Construct()
{
    At<Nx::CFeeler>(this, 0x1C).Construct();
    return this;
}

// 0x00528890
void WalkContact::Destroy() { At<CSmtPtr<CCompositeObject>>(this, 0xB4).Release(); }

// 0x005288B0
void CWalkComponent::DestroyBody()
{
    m_vtable = kVtable;
    At<CSmtPtr<CObject>>(this, kObjectRef).Release();
    At<Nx::CCollCache>(this, kCollCache).Destruct();
    for (int i = 7; i >= 0; --i)
        At<WalkContact>(this, kContacts + i * 0xCC).Destroy();
    CBaseComponent::DestroyBody();
}

// 0x00526060
void CWalkComponent::InitFromStructure(Script::CStruct* params)
{
    uint32_t id;
    if (params->GetChecksum(kCameraName, &id, 0)) {
        auto* camera = static_cast<CCompositeObject*>(CompositeObjectManager()->GetObjectByID(id));
        At<CCompositeObject*>(this, kCamera) = camera;
        At<CBaseComponent*>(this, kWalkCamera) = camera->GetComponent(kWalkCameraType);
    }
    At<int>(this, kFrames710) = 0;
}

// 0x00525FB0
void CWalkComponent::Finalize()
{
    static const uint32_t kComponents[] = {0x27D7CD28, 0x7149B753, 0x286A8D26, 0xE594F0A2, 0xD9151D4D,
                                           0x408D2AA9, 0x43686585, 0x5BD63B29, 0x380D909A};
    for (int i = 0; i < 9; ++i)
        At<CBaseComponent*>(this, kInput + 4 * i) = m_object->GetComponent(kComponents[i]);
}

// 0x00525E60
void CWalkComponent::SetCamera(CCompositeObject* camera)
{
    At<CCompositeObject*>(this, kCamera) = camera;
    At<CBaseComponent*>(this, kWalkCamera) = camera->GetComponent(kWalkCameraType);
}

// 0x005260E0
void CWalkComponent::SetState(int state)
{
    if (state == At<int>(this, kState))
        return;
    int previous = At<int>(this, kState);
    At<int>(this, kPreviousState) = previous;
    At<int>(this, kState) = state;
    At<int>(this, kStateTime) = Tmr::GameClockElapsed() + GameVariable<int>(0x00696D60);
    if (previous == 0 || previous == 1) {
        At<bool>(this, kFlag17A8) = false;
        At<bool>(this, kRunning) = false;
    }
    if (previous == 1) {
        At<float>(this, kJumpTime) = 0.0f;
        At<Mth::Vector>(this, kJumpVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
        At<bool>(this, kOnRail) = false;
    }
    if (state == 0) {
        At<float>(this, kHeight1870) = 10000.0f;
        At<bool>(this, kFlag1874) = true;
    }
    if (state == 4)
        At<Mth::Vector>(this, kClimbOffset) = {0.0f, 0.0f, 0.0f, 1.0f};
    if (state != 1)
        At<bool>(this, kPushing) = false;
    if (state == 0 || At<int>(this, kPreviousState) != 1)
        At<bool>(this, kFlag186C) = false;
    if (state == 2) {
        At<float>(this, kClimbScale) = 0.0f;
        if (At<int>(this, kPreviousState) == 1) {
            // Just caught a ledge: the pad's hang controls ignored for hang_control_debounce_time.
            uint8_t* pad = At<uint8_t*>(this, kInput) + 0x20;
            float debounce = WalkParameter(kHangControlDebounceTime);
            uint32_t now = Tmr::GetTime();
            At<uint32_t>(pad, 0x230) = now - Truncate(static_cast<double>(debounce) * kHangDebounceScale);
            debounce = WalkParameter(kHangControlDebounceTime);
            now = Tmr::GetTime();
            At<uint32_t>(pad, 0x234) = now - Truncate(static_cast<double>(debounce) * kHangDebounceScale);
        }
    }
}

// 0x005260C0
void CWalkComponent::ClearModelOffset()
{
    At<Mth::Vector>(At<void*>(this, kModel), 0x1F8) = {0.0f, 0.0f, 0.0f, 1.0f};
}

// 0x00524E10
bool CWalkComponent::UsingDpad()
{
    uint8_t* input = At<uint8_t*>(this, kInput);
    if (!(At<float>(input, 0x228) == 0.0f) || !(At<float>(input, 0x22C) == 0.0f))
        return false;
    return At<bool>(this, 0x98);
}

// 0x00524E60
Mth::Vector* CWalkComponent::GetCameraOffset(Mth::Vector* out)
{
    constexpr float kCameraOffset = -18.0f; // 0x0064C614
    int state = At<int>(this, kState);
    bool offset;
    if (state == 1) {
        int previous = At<int>(this, kPreviousState);
        offset = (previous == 2 || previous == 3) && static_cast<uint32_t>(ClockSince(At<int>(this, kStateTime))) < 150;
    } else {
        offset = state > 1 && state <= 4;
    }
    if (!offset) {
        *out = {0.0f, 0.0f, 0.0f, 1.0f};
        return out;
    }
    const Mth::Vector& facing = At<Mth::Vector>(this, kFacing);
    *out = {facing.x * kCameraOffset, facing.y * kCameraOffset, facing.z * kCameraOffset, facing.w * kCameraOffset};
    return out;
}

// 0x00528560
Mth::Vector* CWalkComponent::GetFacing(Mth::Vector* out)
{
    *out = At<Mth::Vector>(this, kFacing);
    return out;
}

// 0x00528220
Mth::Vector* CWalkComponent::GetTurnedFacing(Mth::Vector* out, int index)
{
    const Mth::Vector& facing = At<Mth::Vector>(this, kFacing);
    float x, z;
    TurnFlat(static_cast<double>(index) * kSeventhTurn, facing.x, facing.z, &x, &z);
    out->y = 0.0f;
    out->w = 1.0f;
    out->x = x;
    out->z = z;
    return out;
}

// 0x00528370
void CWalkComponent::UpdateRunAdjustment()
{
    uint8_t* score = At<uint8_t*>(m_object->GetComponent(kSkaterScore), 0x18);
    if (Truncate(static_cast<double>(At<int>(score, 0x14)) * At<float>(score, 0x60)) > 0) {
        At<float>(this, kRunAdjustment) = 1.0f;
        return;
    }
    float rate = WalkParameter(kRunAdjustRate);
    double adjustment = At<float>(this, kRunAdjustment) - Tmr::GetSlomoFrameSeconds() * rate;
    if (adjustment < 0.0)
        adjustment = 0.0f;
    At<float>(this, kRunAdjustment) = static_cast<float>(adjustment);
}

// 0x00528400
void CWalkComponent::LimitSpeed(uint32_t stopAnim)
{
    if (At<int>(this, kState) != 0)
        return;
    if (static_cast<uint32_t>(ClockSince(At<int>(this, kStateTime))) < 100) {
        At<float>(this, kLastSpeed) = At<float>(this, kSpeed);
        return;
    }
    float speed = At<float>(this, kSpeed);
    float last = At<float>(this, kLastSpeed);
    float limited = speed < last ? speed : last;
    At<float>(this, kSpeed) = limited;
    At<float>(this, kLastSpeed) = speed;
    if (At<uint32_t>(this, kAnimState) == kRun && limited == 0.0f)
        At<uint32_t>(this, kAnimState) = stopAnim == kSkid ? kSkid : kStand;
}

// 0x005284C0
double __cdecl CWalkComponent::GetGravity()
{
    CSkaterCareer* career = Career();
    if (!career->GetCheat(kCheatMoon))
        return WalkParameter(kGravity);
    Career()->SetGlobalFlag(Script::GetInteger(kCheatMoon));
    GameVariable<bool>(0x007CE4FD) = true;
    return static_cast<double>(WalkParameter(kGravity)) * kHalf;
}

// 0x00528280
bool __stdcall CWalkComponent::FindStandPosition(const Mth::Vector* position, Mth::Vector* out, Nx::CFeeler* feeler)
{
    float height = WalkParameter(kFeelerHeight);
    feeler->m_line.start = *position;
    feeler->m_line.start.y = static_cast<float>(static_cast<double>(height) + height + feeler->m_line.start.y);
    feeler->m_line.end = *position;
    feeler->m_line.end.y = feeler->m_line.end.y - WalkParameter(kStandPosSearchDepth);
    if (feeler->GetCollision(true, false)) {
        *out = feeler->m_point;
        return true;
    }
    *out = *position;
    return false;
}

// 0x00525E90
bool CWalkComponent::Walk_Ground(Script::CStruct* /*params*/, Script::CScript* /*script*/)
{
    return At<int>(this, kState) == 0;
}

// 0x00525EA0
bool CWalkComponent::Walk_ScaleAnimSpeed(Script::CStruct* params, Script::CScript* /*script*/)
{
    if (params->ContainsFlag(kOff)) {
        if (At<int>(this, kAnimSpeedScaling)) {
            At<int>(this, kAnimSpeedScaling) = 0;
            float one;
            uint32_t bits = 1;
            std::memcpy(&one, &bits, 4);
            At<CAnimBlenderComponent*>(this, kAnimBlender)->FirstChannel47E850(1.0f, 0.0f, one, 0.0f);
        }
    } else if (params->ContainsFlag(kRun)) {
        At<int>(this, kAnimSpeedScaling) = 1;
    } else if (params->ContainsFlag(kHangMove)) {
        At<int>(this, kAnimSpeedScaling) = 2;
    } else if (params->ContainsFlag(kLadderMove)) {
        At<int>(this, kAnimSpeedScaling) = 3;
        At<float>(this, kLadderDirection) = -1.0f;
    }
    params->GetFloat(kStandardSpeedName, &At<float>(this, kStandardSpeed), 0);
    return true;
}

// 0x00525F60
bool CWalkComponent::ReleaseHang(Script::CStruct* /*params*/, Script::CScript* /*script*/)
{
    GameFunction<void(__thiscall*)(void*)>(0x0043DB90)(this);
    return true;
}

// 0x00528580
bool CWalkComponent::Walk_GetStateTime(Script::CStruct* /*params*/, Script::CScript* script)
{
    script->m_params->AddInteger(kStateTimeName, ClockSince(At<int>(this, kStateTime)));
    return true;
}

// 0x005285C0
bool CWalkComponent::Walk_GetSpeedScale(Script::CStruct* /*params*/, Script::CScript* script)
{
    float speed = At<float>(this, kSpeed);
    uint32_t scale;
    if (speed < WalkParameter(kMaxSlowWalkSpeed))
        scale = kWalkSlow;
    else if (speed < WalkParameter(kMaxFastWalkSpeed))
        scale = kWalkFast;
    else if (speed < WalkParameter(kMaxSlowRunSpeed))
        scale = kRunSlow;
    else
        scale = kRunFast;
    script->m_params->AddChecksum(kSpeedScale, scale);
    return true;
}

// 0x005286A0
bool CWalkComponent::Walk_GetHangInitAnimType(Script::CStruct* /*params*/, Script::CScript* script)
{
    script->m_params->AddChecksum(kHangInitAnimTypeName, At<uint32_t>(this, kHangInitAnimType));
    return true;
}

// 0x005286C0
bool CWalkComponent::Walk_GetStateDuration(Script::CStruct* /*params*/, Script::CScript* script)
{
    uint32_t elapsed = static_cast<uint32_t>(ClockSince(At<int>(this, kStateTime)));
    script->m_params->AddFloat(kStateDuration, static_cast<float>(static_cast<double>(elapsed) * kMs));
    return true;
}

// 0x00528710
bool CWalkComponent::Walk_GetPreviousState(Script::CStruct* /*params*/, Script::CScript* script)
{
    script->m_params->AddChecksum(kPreviousStateName, StateName(At<int>(this, kPreviousState)));
    return true;
}

// 0x00528740
bool CWalkComponent::Walk_GetState(Script::CStruct* /*params*/, Script::CScript* script)
{
    script->m_params->AddChecksum(kStateName, StateName(At<int>(this, kState)));
    return true;
}

// 0x00528770
bool CWalkComponent::Walk_GetHangAngle(Script::CStruct* /*params*/, Script::CScript* script)
{
    // The angle the rail between the two hang nodes climbs (asin of its direction's y, in degrees), signed by
    // which way along it the skater faces.
    CRailManager* rails = At<CRailManager*>(this, kRailManager);
    Mth::Vector from, to;
    const Mth::Vector* a = rails->GetWorldPos(&from, At<const CRailNode*>(this, kHangNode));
    const Mth::Vector* b = rails->GetWorldPos(&to, At<const CRailNode*>(this, kHangNextNode));
    float dx = b->x - a->x;
    float dy = b->y - a->y;
    double dzFull = static_cast<double>(b->z) - a->z;
    float dz = static_cast<float>(dzFull);
    // (z's square from its unrounded difference times its rounded one, as the original's x87 code)
    double length = X87::Sqrt((dzFull * dz + static_cast<double>(dy) * dy) + static_cast<double>(dx) * dx);
    if (length > 0.0) {
        double inverse = 1.0 / length;
        dx = static_cast<float>(dx * inverse);
        dy = static_cast<float>(dy * inverse);
        dz = static_cast<float>(dz * inverse);
    }
    const Mth::Vector& facing = At<Mth::Vector>(this, kFacing);
    bool negate = static_cast<double>(dz) * facing.x - static_cast<double>(dx) * facing.z < 0.0;
    script->m_params->AddFloat(kHangAngle, AsinDegrees(dy, negate));
    return true;
}

// 0x00528860
bool CWalkComponent::Walk_SetForwardControlLock(Script::CStruct* params, Script::CScript* /*script*/)
{
    At<bool>(this, kForwardControlLock) = params->ContainsFlag(kOn);
    return true;
}

} // namespace Obj

REPLACE(0x00525E40, Obj::CWalkComponent::RegisterMemberFunctions);
REPLACE(0x00528880, &Obj::WalkContact::Construct);
REPLACE(0x00528890, &Obj::WalkContact::Destroy);
REPLACE(0x005288B0, &Obj::CWalkComponent::DestroyBody);
REPLACE(0x00526060, &Obj::CWalkComponent::InitFromStructure);
REPLACE(0x00525FB0, &Obj::CWalkComponent::Finalize);
REPLACE(0x00525E60, &Obj::CWalkComponent::SetCamera);
REPLACE(0x005260E0, &Obj::CWalkComponent::SetState);
REPLACE(0x005260C0, &Obj::CWalkComponent::ClearModelOffset);
REPLACE(0x00524E10, &Obj::CWalkComponent::UsingDpad);
REPLACE(0x00524E60, &Obj::CWalkComponent::GetCameraOffset);
REPLACE(0x00528560, &Obj::CWalkComponent::GetFacing);
REPLACE(0x00528220, &Obj::CWalkComponent::GetTurnedFacing);
REPLACE(0x00528370, &Obj::CWalkComponent::UpdateRunAdjustment);
REPLACE(0x00528400, &Obj::CWalkComponent::LimitSpeed);
REPLACE(0x005284C0, Obj::CWalkComponent::GetGravity);
REPLACE(0x00528280, Obj::CWalkComponent::FindStandPosition);
REPLACE(0x00525E90, &Obj::CWalkComponent::Walk_Ground);
REPLACE(0x00525EA0, &Obj::CWalkComponent::Walk_ScaleAnimSpeed);
REPLACE(0x00525F60, &Obj::CWalkComponent::ReleaseHang);
REPLACE(0x00528580, &Obj::CWalkComponent::Walk_GetStateTime);
REPLACE(0x005285C0, &Obj::CWalkComponent::Walk_GetSpeedScale);
REPLACE(0x005286A0, &Obj::CWalkComponent::Walk_GetHangInitAnimType);
REPLACE(0x005286C0, &Obj::CWalkComponent::Walk_GetStateDuration);
REPLACE(0x00528710, &Obj::CWalkComponent::Walk_GetPreviousState);
REPLACE(0x00528740, &Obj::CWalkComponent::Walk_GetState);
REPLACE(0x00528770, &Obj::CWalkComponent::Walk_GetHangAngle);
REPLACE(0x00528860, &Obj::CWalkComponent::Walk_SetForwardControlLock);
