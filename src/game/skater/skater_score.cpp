// The skater's score component (Obj::CSkaterScoreComponent): see skater_score.h.

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
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x0064C090);
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

// The score (Mdl::Score).
Mdl::Score* ScoreOf(uint8_t* score) { return reinterpret_cast<Mdl::Score*>(score); }
int TotalScore(uint8_t* score) { return ScoreOf(score)->GetTotalScore(); }
// The score so far times the multiplier (+0x14 * +0x60), truncated as the original's _ftol.
int ScorePot(uint8_t* score)
{
    double pot = static_cast<double>(Field<int>(score, 0x14)) * Field<float>(score, 0x60);
    return static_cast<int>(static_cast<int64_t>(pot));
}
int LastScoreLanded(uint8_t* score) { return Field<int>(score, 0x20); }

// The career (the skate module 0x007CE478's +0x20; not rewritten here).
uint8_t* Career() { return Field<uint8_t*>(reinterpret_cast<uint8_t*>(Mdl::g_skateModule), 0x20); }

// The unnamed integer (asserted). Without one the original's variable keeps what was in its stack slot - the
// `params` argument it reuses - so that's the value then.
int NumberParameter(Script::CStruct* params)
{
    int value = static_cast<int>(reinterpret_cast<uintptr_t>(params));
    params->GetInteger(uint32_t{0}, &value, 1);
    return value;
}

} // namespace

// 0x00518B60
CBaseComponent* __cdecl CSkaterScoreComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CSkaterScoreComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CSkaterScoreComponent*>(memory);
    component->CBaseComponent::Construct();
    component->m_vtable = kVtable;
    component->m_type = kSkaterScore;
    component->m_score = nullptr;
    return component;
}

// 0x00518BE0
void CSkaterScoreComponent::DestroyBody()
{
    m_vtable = kVtable;
    if (uint8_t* score = m_score) {
        ScoreOf(score)->DestroyBody(); // (its destructor)
        Mem::Manager::Get()->Free(score);
    }
    CBaseComponent::DestroyBody();
}

// 0x00518C50
CSkaterScoreComponent* CSkaterScoreComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00518660
void CSkaterScoreComponent::InitFromStructure(Script::CStruct* /*params*/)
{
    if (m_score)
        return;
    void* memory = Mem::Calloc(0x1550);
    m_score = memory ? reinterpret_cast<uint8_t*>(static_cast<Mdl::Score*>(memory)->Construct()) : nullptr; // (its constructor)
    ScoreOf(m_score)->SetSkaterId(static_cast<int16_t>(m_object->m_id));
}

// 0x005186E0
void CSkaterScoreComponent::Update()
{
    if (!m_object->m_unknownBC) {
        VirtualSuspend(true);
        return;
    }
    ScoreOf(m_score)->Update();
    bool alwaysSpecial = Field<uint8_t>(m_object, 0x168) != 0 ||
                         reinterpret_cast<CSkaterCareer*>(Career())->GetCheat(kCheatAlwaysSpecial);
    if (alwaysSpecial) {
        uint8_t* career = Career();
        reinterpret_cast<CSkaterCareer*>(career)->SetGlobalFlag(Script::GetInteger(kCheatAlwaysSpecial));
        ScoreOf(m_score)->FillSpecial();
        GameVariable<uint8_t>(0x007CE4FD) = 1;
    }
}

// 0x00518630
void CSkaterScoreComponent::ResetScore()
{
    ScoreOf(m_score)->Reset();
    ScoreOf(m_score)->SetBalanceMeter(false, 0.0f);
    ScoreOf(m_score)->SetManualMeter(false, 0.0f);
}

// 0x00518760
int CSkaterScoreComponent::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script)
{
    switch (name) {
    case kLastScoreLandedLessThan: {
        int value = NumberParameter(params);
        return LastScoreLanded(m_score) < value ? 1 : 0;
    }
    case kLastScoreLandedGreaterThan: {
        int value = NumberParameter(params);
        return LastScoreLanded(m_score) > value ? 1 : 0;
    }
    case kTotalScoreLessThan: {
        int value = NumberParameter(params);
        return TotalScore(m_score) < value ? 1 : 0;
    }
    case kTotalScoreGreaterThan: {
        int value = NumberParameter(params);
        return TotalScore(m_score) > value ? 1 : 0;
    }
    case kCurrentScorePotLessThan: {
        int value = NumberParameter(params);
        return ScorePot(m_score) < value ? 1 : 0;
    }
    case kCurrentScorePotGreaterThan: {
        int value = NumberParameter(params);
        return ScorePot(m_score) > value ? 1 : 0;
    }
    case kGetSpecialValue:
        script->m_params->AddInteger(kValue, Field<int>(m_score, 0x70));
        return 1;
    case kSetSpecialValue: {
        // (no value: the params pointer, as NumberParameter's)
        int value = static_cast<int>(reinterpret_cast<uintptr_t>(params));
        params->GetInteger(kValue, &value, 0);
        Field<int>(m_score, 0x70) = value;
        return 1;
    }
    case kGotSpecial:
        return Field<uint8_t>(m_score, 0x74) ? 1 : 0;
    case kForceSpecial:
        ScoreOf(m_score)->FillSpecial();
        return 1;
    case kGetTotalScore:
        script->m_params->AddInteger(kTotalScore, TotalScore(m_score));
        return 1;
    case kGetNumberOfNonGapTricks:
        script->m_params->AddInteger(kNumberOfNonGapTricks, ScoreOf(m_score)->CountNonGapTricks());
        return 1;
    case kTweakTrick: {
        int tweak = 0;
        params->GetInteger(uint32_t{0}, &tweak, 0);
        ScoreOf(m_score)->TweakTrick(tweak);
        return 1;
    }
    case kIncrementScore:
        ScoreOf(m_score)->SetTotalScore(TotalScore(m_score) + 1);
        return 1;
    case kIsLatestTrick: {
        // A trick by key combo or by name, with its spin (half turns) and taps (1 by default).
        uint32_t keyCombo;
        const char* text = nullptr;
        bool byKeyCombo = params->GetChecksum(kKeycombo, &keyCombo, 0);
        if (!byKeyCombo && !params->GetString(kTrickText, &text, 0))
            return 2;
        int spin = 0;
        if (params->GetInteger(kSpin, &spin, 0))
            spin /= 180;
        int taps = 1;
        params->GetInteger(kNumTaps, &taps, 0);
        bool latest = byKeyCombo ? ScoreOf(m_score)->LastTrickIsKeyCombo(keyCombo, spin, false, taps)
                                 : ScoreOf(m_score)->LastTrickIs(Crc::GenerateCRCFromString(text), spin, false, taps);
        return latest ? 1 : 0;
    }
    default:
        return 2;
    }
}

} // namespace Obj

REPLACE(0x00518B60, Obj::CSkaterScoreComponent::Create);
REPLACE(0x00518BE0, &Obj::CSkaterScoreComponent::DestroyBody);
REPLACE(0x00518C50, &Obj::CSkaterScoreComponent::Destroy);
REPLACE(0x00518660, &Obj::CSkaterScoreComponent::InitFromStructure);
REPLACE(0x005186E0, &Obj::CSkaterScoreComponent::Update);
REPLACE(0x00518630, &Obj::CSkaterScoreComponent::ResetScore);
REPLACE(0x00518760, &Obj::CSkaterScoreComponent::CallMemberFunction);
