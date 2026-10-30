// The skater's balance trick component (Obj::CSkaterBalanceTrickComponent): see balance_trick.h.

#include "game/skater/balance_trick.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/obj/anim_blender.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/skate/score.h"
#include "game/skater/skater.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "game/skater/skitch.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "hook/game_code.h"
#include "game/obj/camera_lookaround.h"
#include "hook/game_global.h"

namespace Obj {

GAME_GLOBAL(uint32_t[12], g_balanceTrickMemberFunctions, 0x00683238,
    0x6DF82E70, 0x589E5FB3, 0, 0, 0, 0,
    0, 0x589E5FB3, 0, 0, 0, 0);

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

CSkater* SkaterOf(CCompositeObject* object) { return static_cast<CSkater*>(object); }

Script::CStruct* NewStruct()
{
    Script::CStruct* structure = Script::NewStruct();
    return structure ? structure->Init() : nullptr;
}

} // namespace

// 0x004F9170
CBaseComponent* __cdecl CSkaterBalanceTrickComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CSkaterBalanceTrickComponent));
    return memory ? static_cast<CSkaterBalanceTrickComponent*>(memory)->Construct() : nullptr;
}

// 0x004F8CD0
void __cdecl CSkaterBalanceTrickComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), g_balanceTrickMemberFunctions,
                                        0x18);
}

// 0x004F8F80
CSkaterBalanceTrickComponent* CSkaterBalanceTrickComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = kVtable;
    m_manual.Construct();
    m_grind.Construct();
    m_lip.Construct();
    m_skitch.Construct();
    m_type = kType;
    mp_animBlender = nullptr;
    mp_params = nullptr;
    return this;
}

// 0x004F8CF0
void CSkaterBalanceTrickComponent::DestroyBody()
{
    m_vtable = kVtable;
    m_skitch.DestroyBody();
    m_lip.DestroyBody();
    m_grind.DestroyBody();
    m_manual.DestroyBody();
    CBaseComponent::DestroyBody();
}

// 0x004F9010
CSkaterBalanceTrickComponent* CSkaterBalanceTrickComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004F9040
void CSkaterBalanceTrickComponent::InitFromStructure(Script::CStruct* /*params*/)
{
    m_manual.Init(m_object);
    m_grind.Init(m_object);
    m_lip.Init(m_object);
    m_skitch.Init(m_object);
}

// 0x004F9080
void CSkaterBalanceTrickComponent::Finalize()
{
    mp_animBlender = static_cast<CAnimBlenderComponent*>(m_object->GetComponent(kAnimBlender));
}

// 0x004F8D70
int CSkaterBalanceTrickComponent::DoingBalanceTrick(Script::CStruct* /*params*/, Script::CScript* /*script*/)
{
    return m_started ? 1 : 0;
}

// 0x004F8D80
void CSkaterBalanceTrickComponent::ClearBestTimes()
{
    m_manual.ClearBestTime();
    m_grind.ClearBestTime();
    m_lip.ClearBestTime();
    m_skitch.ClearBestTime();
}

// 0x004F8DB0
void CSkaterBalanceTrickComponent::KeepBestTimes()
{
    m_manual.KeepBestTime();
    m_grind.KeepBestTime();
    m_lip.KeepBestTime();
    m_skitch.KeepBestTime();
}

// 0x004F8DE0
void CSkaterBalanceTrickComponent::Reset()
{
    m_manual.Reset();
    m_grind.Reset();
    m_skitch.Reset();
    m_lip.Reset();
    m_trick = 0;
    m_started = false;
}

// 0x004F8EE0
void CSkaterBalanceTrickComponent::Stop()
{
    m_manual.Stop();
    m_grind.Stop();
    m_lip.Stop();
    m_skitch.Stop();
    m_trick = 0;
    m_started = false;
}

CBalance* CSkaterBalanceTrickComponent::Current()
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
void CSkaterBalanceTrickComponent::GetButtons(int* count, uint32_t* buttons) const
{
    const CBalance* balance = const_cast<CSkaterBalanceTrickComponent*>(this)->Current();
    if (!balance) {
        *count = 0;
        return;
    }
    *count = 2;
    buttons[0] = balance->m_leftButton;
    buttons[1] = balance->m_rightButton;
}

// 0x004F8F20
float CSkaterBalanceTrickComponent::GetLean() const
{
    const CBalance* balance = const_cast<CSkaterBalanceTrickComponent*>(this)->Current();
    return balance ? balance->m_lean : 0.0f;
}

// 0x004F90A0
float CSkaterBalanceTrickComponent::GetPhysicsValue(uint32_t name)
{
    if (mp_params)
        return SkaterOf(m_object)->GetScaledParam(name, mp_params);
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
    CSkater* skater = SkaterOf(m_object);
    return skater->GetScaledParam(name, Script::GetStructure(structure));
}

// 0x004F91D0
void CSkaterBalanceTrickComponent::ClearParams()
{
    if (Script::CStruct* params = mp_params) {
        params->Clear();
        Script::DeleteStruct(params);
        mp_params = nullptr;
    }
}

// 0x004F9370
void CSkaterBalanceTrickComponent::SetParams(Script::CStruct* params)
{
    if (!mp_params)
        mp_params = NewStruct();
    mp_params->Clear();
    mp_params->AppendStructureIfAny(params);
}

// 0x004F9200
void CSkaterBalanceTrickComponent::SetWobble(Script::CStruct* wobble)
{
    if (!m_object->m_script) {
        void* memory = Script::g_scriptPools.Allocate(0xF4);
        m_object->m_script = memory ? static_cast<Script::CScript*>(memory)->Construct() : nullptr;
    }
    float values[5];
    Script::CStruct* value = nullptr; // (not cleared again: a missing one leaves the last found)
    for (int i = 0; i < 5; ++i) {
        wobble->GetStructure(kWobbleNames[i], &value, 1);
        values[i] = SkaterOf(m_object)->GetScaledValue(value);
    }
    mp_animBlender->FirstChannel47E450(values, 1);
}

// 0x004F9420
int CSkaterBalanceTrickComponent::CallMemberFunction(uint32_t name, Script::CStruct* params,
                                                     Script::CScript* /*script*/)
{
    switch (name) {
    case kSwitchOffBalanceMeter: {
        auto* score = m_object->GetComponent(kSkaterScore);
        Mdl::Score* display = *reinterpret_cast<Mdl::Score**>(reinterpret_cast<uint8_t*>(score) + 0x18);
        display->SetBalanceMeter(false, 0.0f);
        display->SetManualMeter(false, 0.0f);
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
        params->GetFloat(kTimeAdd, &timeAdd, 0);
        params->GetFloat(kLeanAdd, &leanAdd, 0);
        params->GetFloat(kSpeedAdd, &speedAdd, 0);
        params->GetFloat(kTimeMult, &timeMult, 0);
        params->GetFloat(kLeanMult, &leanMult, 0);
        params->GetFloat(kSpeedMult, &speedMult, 0);
        // (an unknown type has no balance: the original crashes here)
        CBalance* balance = Current();
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
        if (params->ContainsFlag(kWobble)) {
            m_manual.ShowMeter();
            m_grind.ShowMeter();
            m_lip.ShowMeter();
            m_skitch.ShowMeter();
        }
        Script::CStruct* wobble = Script::GetStructure(kDefaultWobbleParams); // (asserted)
        params->GetStructure(kWobbleParams, &wobble, 0);
        SetWobble(wobble);
        return 1;
    }
    case kStopBalanceTrick:
        Stop();
        return 1;
    case kDoBalanceTrick: {
        ClearParams();
        Script::CStruct* balanceParams = nullptr;
        if (params->GetStructure(kBalanceParams, &balanceParams, 0))
            SetParams(balanceParams);
        int tweak = 0;
        params->GetInteger(kTweak, &tweak, 0);
        uint32_t buttonA = 0, buttonB = 0, type = 0;
        params->GetChecksum(kButtonA, &buttonA, 1);
        params->GetChecksum(kButtonB, &buttonB, 1);
        params->GetChecksum(kTypeName, &type, 0);
        // The same trick (a manual and a nose manual count as one) or a tap: carried on.
        if (m_trick == type || (m_trick == kManual && type == kNoseManual) || (m_trick == kNoseManual && type == kManual))
            return 1;
        int tap;
        if (params->GetInteger(kIsATap, &tap, 0))
            return 1;
        m_trick = type;
        bool doFlip = params->ContainsFlag(kDoFlipCheck);
        bool backwards = params->ContainsFlag(kPlayRangeAnimBackwards);
        bool clearCheese = params->ContainsFlag(kClearCheese);
        CBalance* balance = Current();
        if (!balance)
            return 1;
        if (clearCheese)
            balance->Reset();
        balance->KeepBestTime();
        balance->Start(buttonA, buttonB, static_cast<uint32_t>(tweak), doFlip, backwards);
        return 1;
    }
    default:
        return 2;
    }
}

} // namespace Obj

REPLACE(0x004F9170, Obj::CSkaterBalanceTrickComponent::Create);
REPLACE(0x004F8CD0, Obj::CSkaterBalanceTrickComponent::RegisterMemberFunctions);
REPLACE(0x004F8F80, &Obj::CSkaterBalanceTrickComponent::Construct);
REPLACE(0x004F8CF0, &Obj::CSkaterBalanceTrickComponent::DestroyBody);
REPLACE(0x004F9010, &Obj::CSkaterBalanceTrickComponent::Destroy);
REPLACE(0x004F9040, &Obj::CSkaterBalanceTrickComponent::InitFromStructure);
REPLACE(0x004F9080, &Obj::CSkaterBalanceTrickComponent::Finalize);
REPLACE(0x004F9420, &Obj::CSkaterBalanceTrickComponent::CallMemberFunction);
REPLACE(0x004F8D70, &Obj::CSkaterBalanceTrickComponent::DoingBalanceTrick);
REPLACE(0x004F8D80, &Obj::CSkaterBalanceTrickComponent::ClearBestTimes);
REPLACE(0x004F8DB0, &Obj::CSkaterBalanceTrickComponent::KeepBestTimes);
REPLACE(0x004F8DE0, &Obj::CSkaterBalanceTrickComponent::Reset);
REPLACE(0x004F8EE0, &Obj::CSkaterBalanceTrickComponent::Stop);
REPLACE(0x004F8E20, &Obj::CSkaterBalanceTrickComponent::GetButtons);
REPLACE(0x004F8F20, &Obj::CSkaterBalanceTrickComponent::GetLean);
REPLACE(0x004F90A0, &Obj::CSkaterBalanceTrickComponent::GetPhysicsValue);
REPLACE(0x004F91D0, &Obj::CSkaterBalanceTrickComponent::ClearParams);
REPLACE(0x004F9370, &Obj::CSkaterBalanceTrickComponent::SetParams);
REPLACE(0x004F9200, &Obj::CSkaterBalanceTrickComponent::SetWobble);

namespace Obj {
namespace {

GAME_VTABLE(kVtable, 0x0064BA30,
    &Obj::CSkaterBalanceTrickComponent::Destroy,
    &Obj::ComponentWithoutUpdate::Update,
    &Obj::CSkaterBalanceTrickComponent::InitFromStructure,
    &Obj::CSkaterBalanceTrickComponent::Finalize,
    Stubs::DoNothing,
    Obj::RefreshFromStructure);

} // namespace
} // namespace Obj
