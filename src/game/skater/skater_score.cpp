// The skater's score component (GameObjects::CRiderPointKeeper): see skater_score.h.

#include "game/skater/skater_score.h"

#include <cstdint>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skate/score.h"
#include "game/skate/skate_module.h"
#include "game/core/code_address.h"
#include "game/net/gamenet_manager.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

namespace {

extern void** const kVtable;
constexpr uint32_t kSkaterScore = 0xD262802F;
constexpr uint32_t kCheatAlwaysSpecial = 0xEEFECF1B;
// Member functions
constexpr uint32_t kLastScoreLandedLessThan = 0x3C445A82, kGetSpecialValue = 0x3AF0AEDC, kIsLatestTrick = 0x2877B61A,
                   kGotSpecial = 0x5589B902, kCurrentScorePotLessThan = 0x56645FC6, kForceSpecial = 0x5E373400,
                   kTotalScoreGreaterThan = 0x8BF43831, kGetTotalScore = 0x8F2FE3D3,
                   kGetNumberOfNonGapTricks = 0xC068EA59, kTweakTrick = 0xBE3E5463, kIncrementScore = 0xBC30CA4E,
                   kCurrentScorePotGreaterThan = 0xD19BDB50, kSetSpecialValue = 0xF4EE54F0,
                   kLastScoreLandedGreaterThan = 0xE0112AAB, kTotalScoreLessThan = 0xDF14BD98;
// Parameters and results
constexpr uint32_t kValue = 0xE288A7CB, kKeycombo = 0x95E16467, kSpin = 0xEDF5DB70, kNumTaps = 0xC82CF71B,
                   kTrickText = 0x3EAFA520, kTotalScore = 0xEE5B2B48, kNumberOfNonGapTricks = 0xFFE7E02C;

template <typename T>
T& Field(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// The score (GameModules::Score).
GameModules::Score* ScoreOf(uint8_t* score) { return reinterpret_cast<GameModules::Score*>(score); }
int TotalScore(uint8_t* score) { return ScoreOf(score)->GetTotalScore(); }
// The score so far times the multiplier (+0x14 * +0x60), truncated as the original's _ftol.
int ScorePot(uint8_t* score)
{
    double pot = static_cast<double>(Field<int>(score, 0x14)) * Field<float>(score, 0x60);
    return static_cast<int>(static_cast<int64_t>(pot));
}
int LastScoreLanded(uint8_t* score) { return Field<int>(score, 0x20); }

// The career (the skate module 0x007CE478's +0x20; not rewritten here).
uint8_t* Career() { return Field<uint8_t*>(reinterpret_cast<uint8_t*>(GameModules::g_skateModule), 0x20); }

// The unnamed integer (asserted). Without one the original's variable keeps what was in its stack slot - the
// `params` argument it reuses - so that's the value then.
int NumberParameter(Scripts::CStruct* params)
{
    int value = static_cast<int>(reinterpret_cast<uintptr_t>(params));
    params->FetchIntValue(uint32_t{0}, &value, 1);
    return value;
}

} // namespace

// 0x00518B60
CObjectModule* __cdecl CRiderPointKeeper::Create()
{
    void* memory = MemSys::AllocZeroed(sizeof(CRiderPointKeeper));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CRiderPointKeeper*>(memory);
    component->CObjectModule::Construct();
    component->m_vtable = kVtable;
    component->m_type = kSkaterScore;
    component->m_pointsHeld = nullptr;
    return component;
}

// 0x00518BE0
void CRiderPointKeeper::DestroyBody()
{
    m_vtable = kVtable;
    if (uint8_t* score = m_pointsHeld) {
        ScoreOf(score)->DestroyBody(); // (its destructor)
        MemSys::Manager::Get()->Free(score);
    }
    CObjectModule::DestroyBody();
}

// 0x00518C50
CRiderPointKeeper* CRiderPointKeeper::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x00518660
void CRiderPointKeeper::ConfigureFromData(Scripts::CStruct* /*params*/)
{
    if (m_pointsHeld)
        return;
    void* memory = MemSys::AllocZeroed(0x1550);
    m_pointsHeld = memory ? reinterpret_cast<uint8_t*>(static_cast<GameModules::Score*>(memory)->Construct()) : nullptr; // (its constructor)
    ScoreOf(m_pointsHeld)->SetRiderNumber(static_cast<int16_t>(m_object->m_id));
}

// 0x005186E0
void CRiderPointKeeper::Update()
{
    if (!m_object->m_unknownBC) {
        VirtualSuspend(true);
        return;
    }
    ScoreOf(m_pointsHeld)->Update();
    bool alwaysSpecial = Field<uint8_t>(m_object, 0x168) != 0 ||
                         reinterpret_cast<CRiderStoryProgress*>(Career())->IsCodeOn(kCheatAlwaysSpecial);
    if (alwaysSpecial) {
        uint8_t* career = Career();
        reinterpret_cast<CRiderStoryProgress*>(career)->SetGlobalFlag(Scripts::FetchIntValue(kCheatAlwaysSpecial));
        ScoreOf(m_pointsHeld)->FillSpecial();
        reinterpret_cast<uint8_t&>(OnlinePlay::g_cheatFlag) = 1;
    }
}

// 0x00518630
void CRiderPointKeeper::ResetScore()
{
    ScoreOf(m_pointsHeld)->Reset();
    ScoreOf(m_pointsHeld)->SetTeeterGauge(false, 0.0f);
    ScoreOf(m_pointsHeld)->SetWheelieGauge(false, 0.0f);
}

// 0x00518760
int CRiderPointKeeper::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script)
{
    switch (name) {
    case kLastScoreLandedLessThan: {
        int value = NumberParameter(params);
        return LastScoreLanded(m_pointsHeld) < value ? 1 : 0;
    }
    case kLastScoreLandedGreaterThan: {
        int value = NumberParameter(params);
        return LastScoreLanded(m_pointsHeld) > value ? 1 : 0;
    }
    case kTotalScoreLessThan: {
        int value = NumberParameter(params);
        return TotalScore(m_pointsHeld) < value ? 1 : 0;
    }
    case kTotalScoreGreaterThan: {
        int value = NumberParameter(params);
        return TotalScore(m_pointsHeld) > value ? 1 : 0;
    }
    case kCurrentScorePotLessThan: {
        int value = NumberParameter(params);
        return ScorePot(m_pointsHeld) < value ? 1 : 0;
    }
    case kCurrentScorePotGreaterThan: {
        int value = NumberParameter(params);
        return ScorePot(m_pointsHeld) > value ? 1 : 0;
    }
    case kGetSpecialValue:
        script->m_params->PutInt(kValue, Field<int>(m_pointsHeld, 0x70));
        return 1;
    case kSetSpecialValue: {
        // (no value: the params pointer, as NumberParameter's)
        int value = static_cast<int>(reinterpret_cast<uintptr_t>(params));
        params->FetchIntValue(kValue, &value, 0);
        Field<int>(m_pointsHeld, 0x70) = value;
        return 1;
    }
    case kGotSpecial:
        return Field<uint8_t>(m_pointsHeld, 0x74) ? 1 : 0;
    case kForceSpecial:
        ScoreOf(m_pointsHeld)->FillSpecial();
        return 1;
    case kGetTotalScore:
        script->m_params->PutInt(kTotalScore, TotalScore(m_pointsHeld));
        return 1;
    case kGetNumberOfNonGapTricks:
        script->m_params->PutInt(kNumberOfNonGapTricks, ScoreOf(m_pointsHeld)->CountNonGapTricks());
        return 1;
    case kTweakTrick: {
        int tweak = 0;
        params->FetchIntValue(uint32_t{0}, &tweak, 0);
        ScoreOf(m_pointsHeld)->TweakTrick(tweak);
        return 1;
    }
    case kIncrementScore:
        ScoreOf(m_pointsHeld)->SetGrandTotal(TotalScore(m_pointsHeld) + 1);
        return 1;
    case kIsLatestTrick: {
        // A trick by key combo or by name, with its spin (half turns) and taps (1 by default).
        uint32_t keyCombo;
        const char* text = nullptr;
        bool byKeyCombo = params->ReadHashValue(kKeycombo, &keyCombo, 0);
        if (!byKeyCombo && !params->GetString(kTrickText, &text, 0))
            return 2;
        int spin = 0;
        if (params->FetchIntValue(kSpin, &spin, 0))
            spin /= 180;
        int taps = 1;
        params->FetchIntValue(kNumTaps, &taps, 0);
        bool latest = byKeyCombo ? ScoreOf(m_pointsHeld)->LastTrickIsKeyCombo(keyCombo, spin, false, taps)
                                 : ScoreOf(m_pointsHeld)->LastTrickIs(Checksums::HashFromText(text), spin, false, taps);
        return latest ? 1 : 0;
    }
    default:
        return 2;
    }
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CRiderPointKeeper::Destroy,
         &GameObjects::CRiderPointKeeper::Update,
         &GameObjects::CRiderPointKeeper::ConfigureFromData,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::CRiderPointKeeper::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
