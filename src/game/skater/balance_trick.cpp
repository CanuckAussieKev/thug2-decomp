// The skater's balance trick component (GameObjects::CRiderTeeterMoves): see balance_trick.h.

#include "game/skater/balance_trick.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/slab_bank.h"
#include "game/obj/anim_blender.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/param_struct.h"
#include "game/skate/point_tally.h"
#include "game/skater/rider.h"
#include "game/core/code_address.h"
#include "game/skater/skitch.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

uint32_t g_balanceTrickMemberFunctions[12]{
    0x6DF82E70, 0x589E5FB3, 0, 0, 0, 0,
    0, 0x589E5FB3, 0, 0, 0, 0};

namespace {

extern void** const kVtable;

constexpr uint32_t kAnimBlender = 0x7149B753, kSkaterScore = 0xD262802F;

// Member functions
constexpr uint32_t kSwitchOffBalanceMeter = 0x78E669AB, kSwitchOnBalanceMeter = 0x73727701,
                   kStartBalanceTrick = 0x6CC475B7, kAdjustBalance = 0x7A0DCD4B, kSetWobbleDetails = 0xEA6D0EFD,
                   kStopBalanceTrick = 0xE553A5B8, kDoBalanceTrick = 0x7E92670C;
// Parameters
constexpr uint32_t kTimeAdd = 0x3A4E9413, kLeanAdd = 0x0B4CECC1, kSpeedAdd = 0xEB9ECABB, kTimeMult = 0x4C074698,
                   kLeanMult = 0x24EBF718, kSpeedMult = 0x94DBBD1C, kWobble = 0x6D941203,
                   kDefaultWobbleParams = 0x5ADB96D4, kWobbleParams = 0xD095379C, kBalanceParams = 0x647FACC2,
                   kTweak = 0x2BFD3D35, kButtonA = 0x4F1A0B01, kButtonB = 0xD6135ABB, kTypeName = 0x7321A8D6,
                   kIsATap = 0x2335E1E1, kDoFlipCheck = 0x1304E677, kPlayRangeAnimBackwards = 0x08FE31ED,
                   kClearCheese = 0xBBDB0F78;
// The wobble's values (each a structure for GetScaledValue).
constexpr uint32_t kWobbleNames[5] = {0xFD266A26 /* WobbleAmpA */, 0x642F3B9C /* WobbleAmpB */,
                                      0x0F43FD49 /* WobbleK1 */, 0x964AACF3 /* WobbleK2 */,
                                      0xF90B0824 /* SpazFactor */};
// The physics structures of the types.
constexpr uint32_t kGrindParams = 0x6B7FDADD, kManualParams = 0xB8CC8633, kSkitchParams = 0xFC87CC01,
                   kLipParams = 0xBE615D0A;

CRider* SkaterOf(CGameEntity* object) { return static_cast<CRider*>(object); }

Scripts::CStruct* NewStruct()
{
    Scripts::CStruct* structure = Scripts::NewStruct();
    return structure ? structure->PrepareAll() : nullptr;
}

} // namespace

// 0x004F9170
CObjectModule* __cdecl CRiderTeeterMoves::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CRiderTeeterMoves));
    return memory ? static_cast<CRiderTeeterMoves*>(memory)->MakeNew() : nullptr;
}

// 0x004F8CD0
void __cdecl CRiderTeeterMoves::PublishMethodTable()
{
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), g_balanceTrickMemberFunctions,
                                        0x18);
}

// 0x004F8F80
CRiderTeeterMoves* CRiderTeeterMoves::MakeNew()
{
    CObjectModule::MakeNew();
    m_vtable = kVtable;
    m_manual.MakeNew();
    m_grind.MakeNew();
    m_lip.MakeNew();
    m_skitch.MakeNew();
    m_kindTag = kType;
    mp_animBlender = nullptr;
    mp_params = nullptr;
    return this;
}

// 0x004F8CF0
void CRiderTeeterMoves::DestroyBody()
{
    m_vtable = kVtable;
    m_skitch.DestroyBody();
    m_lip.DestroyBody();
    m_grind.DestroyBody();
    m_manual.DestroyBody();
    CObjectModule::DestroyBody();
}

// 0x004F9010
CRiderTeeterMoves* CRiderTeeterMoves::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004F9040
void CRiderTeeterMoves::ConfigureFromData(Scripts::CStruct* /*params*/)
{
    m_manual.PrepareAll(m_entity);
    m_grind.PrepareAll(m_entity);
    m_lip.PrepareAll(m_entity);
    m_skitch.PrepareAll(m_entity);
}

// 0x004F9080
void CRiderTeeterMoves::Seal()
{
    mp_animBlender = static_cast<CAnimBlenderComponent*>(m_entity->FetchModule(kAnimBlender));
}

// 0x004F8D70
int CRiderTeeterMoves::InBalanceMove(Scripts::CStruct* /*params*/, Scripts::CScript* /*script*/)
{
    return m_started ? 1 : 0;
}

// 0x004F8D80
void CRiderTeeterMoves::ClearBestTimes()
{
    m_manual.ClearBestTime();
    m_grind.ClearBestTime();
    m_lip.ClearBestTime();
    m_skitch.ClearBestTime();
}

// 0x004F8DB0
void CRiderTeeterMoves::KeepBestTimes()
{
    m_manual.KeepBestTime();
    m_grind.KeepBestTime();
    m_lip.KeepBestTime();
    m_skitch.KeepBestTime();
}

// 0x004F8DE0
void CRiderTeeterMoves::Reset()
{
    m_manual.Reset();
    m_grind.Reset();
    m_skitch.Reset();
    m_lip.Reset();
    m_trick = 0;
    m_started = false;
}

// 0x004F8EE0
void CRiderTeeterMoves::Halt()
{
    m_manual.Halt();
    m_grind.Halt();
    m_lip.Halt();
    m_skitch.Halt();
    m_trick = 0;
    m_started = false;
}

CBalance* CRiderTeeterMoves::AtHand()
{
    switch (m_trick) {
    case kManual:
    case kNoseManual:
        return &m_manual;
    case kGrind:
    case kSlide:
        return &m_grind;
    case kSkitch:
        return &m_skitch;
    case kLip:
        return &m_lip;
    default:
        return nullptr;
    }
}

// 0x004F8E20
void CRiderTeeterMoves::GetButtons(int* count, uint32_t* buttons) const
{
    const CBalance* balance = const_cast<CRiderTeeterMoves*>(this)->AtHand();
    if (!balance) {
        *count = 0;
        return;
    }
    *count = 2;
    buttons[0] = balance->m_leftButton;
    buttons[1] = balance->m_rightButton;
}

// 0x004F8F20
float CRiderTeeterMoves::GetLean() const
{
    const CBalance* balance = const_cast<CRiderTeeterMoves*>(this)->AtHand();
    return balance ? balance->m_lean : 0.0f;
}

// 0x004F90A0
float CRiderTeeterMoves::GetPhysicsValue(uint32_t name)
{
    if (mp_params)
        return SkaterOf(m_entity)->GetScaledParam(name, mp_params);
    uint32_t structure;
    switch (m_trick) {
    case kGrind:
    case kSlide:
        structure = kGrindParams;
        break;
    case kManual:
    case kNoseManual:
        structure = kManualParams;
        break;
    case kSkitch:
        structure = kSkitchParams;
        break;
    case kLip:
        structure = kLipParams;
        break;
    default:
        return 0.0f;
    }
    CRider* skater = SkaterOf(m_entity);
    return skater->GetScaledParam(name, Scripts::ReadSubStruct(structure));
}

// 0x004F91D0
void CRiderTeeterMoves::ClearParams()
{
    if (Scripts::CStruct* params = mp_params) {
        params->Clear();
        Scripts::DeleteStruct(params);
        mp_params = nullptr;
    }
}

// 0x004F9370
void CRiderTeeterMoves::ChangeArgs(Scripts::CStruct* params)
{
    if (!mp_params)
        mp_params = NewStruct();
    mp_params->Clear();
    mp_params->AppendStructureIfAny(params);
}

// 0x004F9200
void CRiderTeeterMoves::SetWobble(Scripts::CStruct* wobble)
{
    if (!m_entity->m_runScript) {
        void* memory = Scripts::g_scriptPools.Carve(0xF4);
        m_entity->m_runScript = memory ? static_cast<Scripts::CScript*>(memory)->MakeNew() : nullptr;
    }
    float values[5];
    Scripts::CStruct* value = nullptr; // (not cleared again: a missing one leaves the last found)
    for (int i = 0; i < 5; ++i) {
        wobble->ReadSubStruct(kWobbleNames[i], &value, 1);
        values[i] = SkaterOf(m_entity)->GetScaledValue(value);
    }
    mp_animBlender->FirstChannel47E450(values, 1);
}

// 0x004F9420
int CRiderTeeterMoves::InvokeMethodByName(uint32_t name, Scripts::CStruct* params,
                                                     Scripts::CScript* /*script*/)
{
    switch (name) {
    case kSwitchOffBalanceMeter: {
        auto* score = m_entity->FetchModule(kSkaterScore);
        GameModules::PointTally* display = *reinterpret_cast<GameModules::PointTally**>(reinterpret_cast<uint8_t*>(score) + 0x18);
        display->SetTeeterGauge(false, 0.0f);
        display->SetWheelieGauge(false, 0.0f);
        m_manual.HideDisplay();
        m_grind.HideDisplay();
        m_lip.HideDisplay();
        m_skitch.HideDisplay();
        return 1;
    }
    case kSwitchOnBalanceMeter:
        m_manual.ShowDisplay();
        m_grind.ShowDisplay();
        m_lip.ShowDisplay();
        m_skitch.ShowDisplay();
        return 1;
    case kStartBalanceTrick:
        m_started = true;
        return 1;
    case kAdjustBalance: {
        if (!m_trick)
            return 1;
        float timeAdd = 0.0f, leanAdd = 0.0f, speedAdd = 0.0f, timeMult = 1.0f, leanMult = 1.0f, speedMult = 1.0f;
        params->FetchFloat(kTimeAdd, &timeAdd, 0);
        params->FetchFloat(kLeanAdd, &leanAdd, 0);
        params->FetchFloat(kSpeedAdd, &speedAdd, 0);
        params->FetchFloat(kTimeMult, &timeMult, 0);
        params->FetchFloat(kLeanMult, &leanMult, 0);
        params->FetchFloat(kSpeedMult, &speedMult, 0);
        // (an unknown type has no balance: the original crashes here)
        CBalance* balance = AtHand();
        double time = static_cast<double>(timeMult) * balance->m_railTime + timeAdd;
        balance->m_railTime = time < 0.0 ? 0.0f : static_cast<float>(time);
        // The lean and its speed scaled, then pushed further the way they lean.
        double lean = static_cast<double>(leanMult) * balance->m_lean;
        balance->m_lean = static_cast<float>(lean);
        balance->m_lean = static_cast<float>((lean < 0.0 ? -1.0f : 1.0f) * static_cast<double>(leanAdd) + lean);
        double speed = static_cast<double>(speedMult) * balance->m_leanSpeed;
        balance->m_leanSpeed = static_cast<float>(speed);
        balance->m_leanSpeed = static_cast<float>((speed < 0.0 ? -1.0f : 1.0f) * static_cast<double>(speedAdd) + speed);
        return 1;
    }
    case kSetWobbleDetails: {
        if (params->HasBareFlag(kWobble)) {
            m_manual.ShowMeter();
            m_grind.ShowMeter();
            m_lip.ShowMeter();
            m_skitch.ShowMeter();
        }
        Scripts::CStruct* wobble = Scripts::ReadSubStruct(kDefaultWobbleParams); // (asserted)
        params->ReadSubStruct(kWobbleParams, &wobble, 0);
        SetWobble(wobble);
        return 1;
    }
    case kStopBalanceTrick:
        Halt();
        return 1;
    case kDoBalanceTrick: {
        ClearParams();
        Scripts::CStruct* balanceParams = nullptr;
        if (params->ReadSubStruct(kBalanceParams, &balanceParams, 0))
            ChangeArgs(balanceParams);
        int tweak = 0;
        params->FetchIntValue(kTweak, &tweak, 0);
        uint32_t buttonA = 0, buttonB = 0, type = 0;
        params->ReadHashValue(kButtonA, &buttonA, 1);
        params->ReadHashValue(kButtonB, &buttonB, 1);
        params->ReadHashValue(kTypeName, &type, 0);
        // The same trick (a manual and a nose manual count as one) or a tap: carried on.
        if (m_trick == type || (m_trick == kManual && type == kNoseManual) || (m_trick == kNoseManual && type == kManual))
            return 1;
        int tap;
        if (params->FetchIntValue(kIsATap, &tap, 0))
            return 1;
        m_trick = type;
        bool doFlip = params->HasBareFlag(kDoFlipCheck);
        bool backwards = params->HasBareFlag(kPlayRangeAnimBackwards);
        bool clearCheese = params->HasBareFlag(kClearCheese);
        CBalance* balance = AtHand();
        if (!balance)
            return 1;
        if (clearCheese)
            balance->Reset();
        balance->KeepBestTime();
        balance->Kickoff(buttonA, buttonB, static_cast<uint32_t>(tweak), doFlip, backwards);
        return 1;
    }
    default:
        return 2;
    }
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CRiderTeeterMoves::Teardown,
         &GameObjects::ComponentWithoutUpdate::TickFrame,
         &GameObjects::CRiderTeeterMoves::ConfigureFromData,
         &GameObjects::CRiderTeeterMoves::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &GameObjects::CObjectModule::RunMemberFunction,
         &GameObjects::CRiderTeeterMoves::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
