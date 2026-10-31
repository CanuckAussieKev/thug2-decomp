// Pieces of the Walk component (GameObjects::CFootMovement): see walk_physics.h. From the disassembly of 0x00524E10-
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
#include "game/skater/walk_fields.h"
#include "game/mth/x87_math.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "game/obj/control_pad.h"
#include "game/skate/skate_module.h"
#include "game/core/code_address.h"
#include "game/net/gamenet_manager.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"
#include "game/sys/crt.h"

namespace GameObjects {

uint32_t g_walkMemberFunctions[114]{
    0x893213E5, 0x726E85AA, 0, 0, 0, 0,
    0x5012082E, 0x726E85AA, 0, 0, 0, 0,
    0x9A3CA853, 0x726E85AA, 0, 0, 0, 0,
    0x19702CA8, 0x726E85AA, 0, 0, 0, 0,
    0x8FE2B013, 0x726E85AA, 0, 0, 0, 0,
    0xCE64576C, 0x726E85AA, 0, 0, 0, 0,
    0x83E4BD70, 0x726E85AA, 0, 0, 0, 0,
    0xAF04B983, 0x726E85AA, 0, 0, 0, 0,
    0x56112C03, 0x726E85AA, 0, 0, 0, 0,
    0x9D3EEBE8, 0x726E85AA, 0, 0, 0, 0,
    0xC6CD659E, 0x726E85AA, 0, 0, 0, 0,
    0x55BF1CD6, 0x726E85AA, 0, 0, 0, 0,
    0xE9B1CDE8, 0x726E85AA, 0, 0, 0, 0,
    0xAC7B36C8, 0x726E85AA, 0, 0, 0, 0,
    0x7E01B923, 0x726E85AA, 0, 0, 0, 0,
    0x492EBA50, 0x726E85AA, 0, 0, 0, 0,
    0xF8F754EF, 0x726E85AA, 0, 0, 0, 0,
    0x8BC2C0EC, 0x726E85AA, 0, 0, 0, 0,
    0, 0x726E85AA, 0, 0, 0, 0};

namespace {

using namespace WalkFields;

extern void** const kVtable;

// Script names.
constexpr uint32_t kHangControlDebounceTime = 0xAC96D24B,
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
const uint32_t g_walkStateNames[5]{0x8CF3CB28, 0x9D1F1A2C, 0x74FFC46D, 0x1CB1F465, 0x79BE4637};
uint32_t StateName(int state) { return g_walkStateNames[state]; }

// The skate module's career (0x007CE478's +0x20).
CRiderStoryProgress* Career() { return At<CRiderStoryProgress*>(static_cast<void*>(GameModules::g_skateModule), 0x20); }

// asin (the game's C runtime) of `value` in degrees (x 57.29578), negated when `negate` - the result kept in the
// register until it is rounded to float, as the original.
float AsinDegrees(double value, bool negate)
{
    float result;
    void* function = Crt::AsinX87();
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
void __cdecl CFootMovement::PublishMethodTable()
{
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(kWalk), g_walkMemberFunctions,
                                        0x18);
}

// 0x00528880
WalkContact* WalkContact::MakeNew()
{
    At<Engine3D::CProbeRay>(this, 0x1C).MakeNew();
    return this;
}

// 0x00528890
void WalkContact::Teardown() { At<CSharedRef<CGameEntity>>(this, 0xB4).Release(); }

// 0x005288B0
void CFootMovement::DestroyBody()
{
    m_vtable = kVtable;
    At<CSharedRef<CEntity>>(this, kObjectRef).Release();
    At<Engine3D::CHitMeshCache>(this, kCollCache).Destruct();
    for (int i = 7; i >= 0; --i)
        At<WalkContact>(this, kContacts + i * 0xCC).Teardown();
    CObjectModule::DestroyBody();
}

// 0x00526060
void CFootMovement::ConfigureFromData(Scripts::CStruct* params)
{
    uint32_t id;
    if (params->ReadHashValue(kCameraName, &id, 0)) {
        auto* camera = static_cast<CGameEntity*>(CompositeObjectManager()->LookUpEntity(id));
        At<CGameEntity*>(this, kCamera) = camera;
        At<CObjectModule*>(this, kWalkCamera) = camera->FetchModule(kWalkCameraType);
    }
    At<int>(this, kFrames710) = 0;
}

// 0x00525FB0
void CFootMovement::Seal()
{
    static const uint32_t kComponents[] = {0x27D7CD28, 0x7149B753, 0x286A8D26, 0xE594F0A2, 0xD9151D4D,
                                           0x408D2AA9, 0x43686585, 0x5BD63B29, 0x380D909A};
    for (int i = 0; i < 9; ++i)
        At<CObjectModule*>(this, kInput + 4 * i) = m_entity->FetchModule(kComponents[i]);
}

// 0x00525E60
void CFootMovement::ChooseCamera(CGameEntity* camera)
{
    At<CGameEntity*>(this, kCamera) = camera;
    At<CObjectModule*>(this, kWalkCamera) = camera->FetchModule(kWalkCameraType);
}

// 0x005260E0
void CFootMovement::ChangePhase(int state)
{
    if (state == At<int>(this, kState))
        return;
    int previous = At<int>(this, kState);
    At<int>(this, kPreviousState) = previous;
    At<int>(this, kState) = state;
    At<int>(this, kStateTime) = FrameClock::GameClockElapsed() + g_clockOffset;
    if (previous == 0 || previous == 1) {
        At<bool>(this, kFlag17A8) = false;
        At<bool>(this, kRunning) = false;
    }
    if (previous == 1) {
        At<float>(this, kJumpTime) = 0.0f;
        At<Maths::Vec4>(this, kJumpVelocity) = {0.0f, 0.0f, 0.0f, 1.0f};
        At<bool>(this, kOnRail) = false;
    }
    if (state == 0) {
        At<float>(this, kHeight1870) = 10000.0f;
        At<bool>(this, kFlag1874) = true;
    }
    if (state == 4)
        At<Maths::Vec4>(this, kClimbOffset) = {0.0f, 0.0f, 0.0f, 1.0f};
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
            uint32_t now = FrameClock::GetTime();
            At<uint32_t>(pad, 0x230) = now - Truncate(static_cast<double>(debounce) * kHangDebounceScale);
            debounce = WalkParameter(kHangControlDebounceTime);
            now = FrameClock::GetTime();
            At<uint32_t>(pad, 0x234) = now - Truncate(static_cast<double>(debounce) * kHangDebounceScale);
        }
    }
}

// 0x005260C0
void CFootMovement::ClearModelOffset()
{
    At<Maths::Vec4>(At<void*>(this, kModel), 0x1F8) = {0.0f, 0.0f, 0.0f, 1.0f};
}

// 0x00524E10
bool CFootMovement::UsingDpad()
{
    uint8_t* input = At<uint8_t*>(this, kInput);
    if (!(At<float>(input, 0x228) == 0.0f) || !(At<float>(input, 0x22C) == 0.0f))
        return false;
    return At<bool>(this, 0x98);
}

// 0x00524E60
Maths::Vec4* CFootMovement::GetCameraOffset(Maths::Vec4* out)
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
    const Maths::Vec4& facing = At<Maths::Vec4>(this, kFacing);
    *out = {facing.x * kCameraOffset, facing.y * kCameraOffset, facing.z * kCameraOffset, facing.w * kCameraOffset};
    return out;
}

// 0x00528560
Maths::Vec4* CFootMovement::GetFacing(Maths::Vec4* out)
{
    *out = At<Maths::Vec4>(this, kFacing);
    return out;
}

// 0x00528220
Maths::Vec4* CFootMovement::GetTurnedFacing(Maths::Vec4* out, int index)
{
    const Maths::Vec4& facing = At<Maths::Vec4>(this, kFacing);
    float x, z;
    TurnFlat(static_cast<double>(index) * kSeventhTurn, facing.x, facing.z, &x, &z);
    out->y = 0.0f;
    out->w = 1.0f;
    out->x = x;
    out->z = z;
    return out;
}

// 0x00528370
void CFootMovement::UpdateRunAdjustment()
{
    uint8_t* score = At<uint8_t*>(m_entity->FetchModule(kSkaterScore), 0x18);
    if (Truncate(static_cast<double>(At<int>(score, 0x14)) * At<float>(score, 0x60)) > 0) {
        At<float>(this, kRunAdjustment) = 1.0f;
        return;
    }
    float rate = WalkParameter(kRunAdjustRate);
    double adjustment = At<float>(this, kRunAdjustment) - FrameClock::GetSlomoFrameSeconds() * rate;
    if (adjustment < 0.0)
        adjustment = 0.0f;
    At<float>(this, kRunAdjustment) = static_cast<float>(adjustment);
}

// 0x00528400
void CFootMovement::LimitSpeed(uint32_t stopAnim)
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
double __cdecl CFootMovement::GetGravity()
{
    CRiderStoryProgress* career = Career();
    if (!career->IsCodeOn(kCheatMoon))
        return WalkParameter(kGravity);
    Career()->SetGlobalFlag(Scripts::FetchIntValue(kCheatMoon));
    OnlinePlay::g_cheatFlag = true;
    return static_cast<double>(WalkParameter(kGravity)) * kHalf;
}

// 0x00528280
bool __stdcall CFootMovement::FindStandPosition(const Maths::Vec4* position, Maths::Vec4* out, Engine3D::CProbeRay* feeler)
{
    float height = WalkParameter(kFeelerHeight);
    feeler->m_line.start = *position;
    feeler->m_line.start.y = static_cast<float>(static_cast<double>(height) + height + feeler->m_line.start.y);
    feeler->m_line.end = *position;
    feeler->m_line.end.y = feeler->m_line.end.y - WalkParameter(kStandPosSearchDepth);
    if (feeler->GetHitShape(true, false)) {
        *out = feeler->m_contactPos;
        return true;
    }
    *out = *position;
    return false;
}

// 0x00525E90
bool CFootMovement::Walk_Ground(Scripts::CStruct* /*params*/, Scripts::CScript* /*script*/)
{
    return At<int>(this, kState) == 0;
}

// 0x00525EA0
bool CFootMovement::Walk_ScaleAnimSpeed(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    if (params->HasBareFlag(kOff)) {
        if (At<int>(this, kAnimSpeedScaling)) {
            At<int>(this, kAnimSpeedScaling) = 0;
            float one;
            uint32_t bits = 1;
            std::memcpy(&one, &bits, 4);
            At<CAnimBlenderComponent*>(this, kAnimBlender)->FirstChannel47E850(1.0f, 0.0f, one, 0.0f);
        }
    } else if (params->HasBareFlag(kRun)) {
        At<int>(this, kAnimSpeedScaling) = 1;
    } else if (params->HasBareFlag(kHangMove)) {
        At<int>(this, kAnimSpeedScaling) = 2;
    } else if (params->HasBareFlag(kLadderMove)) {
        At<int>(this, kAnimSpeedScaling) = 3;
        At<float>(this, kLadderDirection) = -1.0f;
    }
    params->FetchFloat(kStandardSpeedName, &At<float>(this, kStandardSpeed), 0);
    return true;
}

// 0x00525F60
bool CFootMovement::Walk_ReleaseHang(Scripts::CStruct* /*params*/, Scripts::CScript* /*script*/)
{
    EndDropFromHang();
    return true;
}

// 0x00528580
bool CFootMovement::Walk_GetStateTime(Scripts::CStruct* /*params*/, Scripts::CScript* script)
{
    script->m_args->PutInt(kStateTimeName, ClockSince(At<int>(this, kStateTime)));
    return true;
}

// 0x005285C0
bool CFootMovement::Walk_GetSpeedScale(Scripts::CStruct* /*params*/, Scripts::CScript* script)
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
    script->m_args->PutNameKey(kSpeedScale, scale);
    return true;
}

// 0x005286A0
bool CFootMovement::Walk_GetHangInitAnimType(Scripts::CStruct* /*params*/, Scripts::CScript* script)
{
    script->m_args->PutNameKey(kHangInitAnimTypeName, At<uint32_t>(this, kHangInitAnimType));
    return true;
}

// 0x005286C0
bool CFootMovement::Walk_GetStateDuration(Scripts::CStruct* /*params*/, Scripts::CScript* script)
{
    uint32_t elapsed = static_cast<uint32_t>(ClockSince(At<int>(this, kStateTime)));
    script->m_args->PushFloat(kStateDuration, static_cast<float>(static_cast<double>(elapsed) * kMs));
    return true;
}

// 0x00528710
bool CFootMovement::Walk_GetPreviousState(Scripts::CStruct* /*params*/, Scripts::CScript* script)
{
    script->m_args->PutNameKey(kPreviousStateName, StateName(At<int>(this, kPreviousState)));
    return true;
}

// 0x00528740
bool CFootMovement::Walk_GetState(Scripts::CStruct* /*params*/, Scripts::CScript* script)
{
    script->m_args->PutNameKey(kStateName, StateName(At<int>(this, kState)));
    return true;
}

// 0x00528770
bool CFootMovement::Walk_GetHangAngle(Scripts::CStruct* /*params*/, Scripts::CScript* script)
{
    // The angle the rail between the two hang nodes climbs (asin of its direction's y, in degrees), signed by
    // which way along it the skater faces.
    CGrindRailHub* rails = At<CGrindRailHub*>(this, kRailManager);
    Maths::Vec4 from, to;
    const Maths::Vec4* a = rails->GetAbsolutePos(&from, At<const CGrindPoint*>(this, kHangNode));
    const Maths::Vec4* b = rails->GetAbsolutePos(&to, At<const CGrindPoint*>(this, kHangNextNode));
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
    const Maths::Vec4& facing = At<Maths::Vec4>(this, kFacing);
    bool negate = static_cast<double>(dz) * facing.x - static_cast<double>(dx) * facing.z < 0.0;
    script->m_args->PushFloat(kHangAngle, AsinDegrees(dy, negate));
    return true;
}

// 0x00528860
bool CFootMovement::Walk_SetForwardControlLock(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    At<bool>(this, kForwardControlLock) = params->HasBareFlag(kOn);
    return true;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&CFootMovement::Teardown,
         &CFootMovement::TickFrame,
         &GameObjects::CFootMovement::ConfigureFromData,
         &GameObjects::CFootMovement::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &GameObjects::CObjectModule::RunMemberFunction,
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
