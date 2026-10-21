// The stats manager component: the story mode's stat challenges. See stats_manager.h.

#include "game/skater/stats_manager.h"

#include <cmath>
#include <cstdint>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/compositemgr.h"
#include "game/obj/objman.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/skate/profile_manager.h"
#include "game/skater/skater.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x0064C294);

constexpr uint32_t kSkaterState = 0x43686585;
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D;

// This frame's state (m_state).
constexpr uint32_t kStateOther = 0x26A7CADF;
constexpr uint32_t kStateLaunched = 0xB39B4F1B; // the skater state's +0x30
constexpr uint32_t kStateAir = 0x439F4704;      // in the air (physics state 1) without +0x30

// Goal kinds.
constexpr uint32_t kRise = 0x39E7537B;          // a jump's rise (inches over 12)
constexpr uint32_t kDrop = 0x7E064AD0;          // a jump's drop
constexpr uint32_t kAirCombo = 0x1DBBB148;      // a combo's score made in the air (landed)
constexpr uint32_t kAirHeight = 0x430B80C4;     // height above the take-off
constexpr uint32_t kAirValue58 = 0x509ACA95;
constexpr uint32_t kComboScore = 0x4EC3CFB5;
constexpr uint32_t kMultiplier = 0x5B6556A4;
constexpr uint32_t kCountA80 = 0x364E8C16;
constexpr uint32_t kCountA84 = 0x68703AE9;
constexpr uint32_t kTrickCount = 0x69B0F836;
constexpr uint32_t kJumpDistance = 0x8F6F9F05; // horizontal
constexpr uint32_t kAirDistance = 0xC3EC3EED;  // horizontal, from take-off to landing
constexpr uint32_t kScore562370 = 0x9068B4E5;

// Balance goal kinds and trick types.
constexpr uint32_t kGrindTime = 0xC1336019;
constexpr uint32_t kManualTime = 0xD9FFC141;
constexpr uint32_t kLipTime = 0xB67CFF14;
constexpr uint32_t kSlide = 0x8D10119D;
constexpr uint32_t kGrind = 0x255ED86F;
constexpr uint32_t kNoseManual = 0x0AC90769;
constexpr uint32_t kManual = 0xEF24413B;
constexpr uint32_t kLip = 0xA549B57B;

constexpr float kFeet = 12.0f; // 0x0064546C

template <typename T>
T& Field(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}

void RunScript(uint32_t name, Script::CStruct* params = nullptr) { Script::RunScript(name, params, nullptr, false, nullptr); }

Script::CStruct* NewParams()
{
    Script::CStruct* memory = Script::NewStruct();
    return memory ? memory->Init() : nullptr;
}
void DeleteParams(Script::CStruct* params)
{
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
}

void* SkateModule() { return GameVariable<void*>(0x007CE478); }
CSkaterProfile* FirstProfile() { return GameFunction<CSkaterProfile*(__thiscall*)(void*, int)>(0x00564FC0)(SkateModule(), 0); }
void* GameFlow() { return GameFunction<void*(__cdecl*)()>(0x005591B0)(); }

// The skater's score component and its counts.
void* Score(CCompositeObject* skater) { return reinterpret_cast<CSkater*>(skater)->GetScore(); }
int CountTricks(void* score, uint32_t trick, int a, int b, int c)
{
    return GameFunction<int(__thiscall*)(void*, uint32_t, int, int, int)>(0x005624C0)(score, trick, a, b, c);
}

// 0x00423680: the distance between two points (z, x, then y; unrounded).
double Distance(const Mth::Vector& a, const Mth::Vector& b)
{
    double dx = static_cast<double>(a.x) - b.x, dy = static_cast<double>(a.y) - b.y, dz = static_cast<double>(a.z) - b.z;
    return std::sqrt((dz * dz + dx * dx) + dy * dy);
}

// a - b, wrapping as the original's 32-bit subtraction.
int Minus(int a, int b) { return static_cast<int>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b)); }

// The original's float-to-int (_ftol: towards zero).
int Truncate(double value) { return static_cast<int>(value); }

} // namespace

// 0x0051CB90
CBaseComponent* __cdecl CStatsManagerComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CStatsManagerComponent));
    return memory ? static_cast<CStatsManagerComponent*>(memory)->Construct() : nullptr;
}

// 0x0051C440
void __cdecl CStatsManagerComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), &GameVariable<uint32_t>(0x00684180), 0x18);
}

// 0x0051C5A0
CStatsManagerComponent* CStatsManagerComponent::Construct()
{
    CBaseComponent::Construct();
    m_skater = nullptr;
    m_skaterState = nullptr;
    m_physicsControl = nullptr;
    m_unknownA9C = 0;
    m_vtable = kVtable;
    m_type = kType;
    m_airStart = {0.0f, 0.0f, 0.0f, 0.0f};
    m_airEnd = {0.0f, 0.0f, 0.0f, 0.0f};
    m_jumpStart = {0.0f, 0.0f, 0.0f, 0.0f};
    m_jumpEnd = {0.0f, 0.0f, 0.0f, 0.0f};
    m_lastAirComboScore = 0;
    return this;
}

// 0x0051C460
void CStatsManagerComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x0051C640
CStatsManagerComponent* CStatsManagerComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0051C470
int CStatsManagerComponent::GetGoalStatus(int index) { return m_goals[index].m_status; }

// 0x0051C480
void CStatsManagerComponent::SetGoalStatus(int index, int status)
{
    m_goals[index].m_status = status;
    if (status == 1)
        m_goalDone = true;
}

// 0x0051C670
void CStatsManagerComponent::InitFromStructure(Script::CStruct*)
{
    m_goalDone = false;
    m_activated = false;
    m_state = 0;
    m_lastState = 0;
    m_airPhase = 0;
    m_airHeight = 0.0f;
    m_airBest = 0;
    m_airStartPoints = 0;
    m_airStartMultiplier = 0.0f;
    m_jumpPhase = 0;
    m_countA80 = 0;
    m_countA84 = 0;
    m_airScore = 0;
    m_firstUpdate = true;
    m_skater = static_cast<CCompositeObject*>(CompositeObjectManager()->GetObjectByID(m_object->m_id));
    if (!m_skater) {
        m_skaterState = nullptr;
        m_physicsControl = nullptr;
    } else {
        m_skaterState = m_skater->GetComponent(kSkaterState);
        m_physicsControl = m_skater->GetComponent(kSkaterPhysicsControl);
    }
    m_difficulty = GameFunction<int(__thiscall*)(void*, bool)>(0x00559050)(GameFlow(), true);
    Dbg::Printf(reinterpret_cast<const char*>(0x0064C2C8), m_difficulty); // "STATSManager: difficulty ... %i"
    if (Script::GetInteger(0xB8688A8Au) == 1)
        m_raiseStats = true;

    Script::CArray* goals = Script::GetArray(0x4E46CC28u);
    int count = static_cast<int>(goals->m_size);
    for (int i = 0; i < count; ++i) {
        auto* source = reinterpret_cast<Script::CStruct*>(goals->GetElement(static_cast<uint32_t>(i)));
        Goal& goal = m_goals[i];
        uint32_t checksum;
        if (source->ContainsComponentNamed(0x0FFD7BBEu)) {
            source->GetChecksum(0x0FFD7BBEu, &checksum, 1);
            goal.m_kind = checksum;
        }
        if (source->ContainsComponentNamed(0xC2455018u)) {
            source->GetChecksum(0xC2455018u, &checksum, 1);
            goal.m_stat = checksum;
        }
        Script::CArray* byDifficulty;
        if (source->ContainsComponentNamed(0xE288A7CBu)) {
            byDifficulty = nullptr;
            source->GetArray(0xE288A7CBu, &byDifficulty, 1);
            goal.m_target = static_cast<int>(byDifficulty->GetElement(static_cast<uint32_t>(m_difficulty)));
        }
        if (source->ContainsComponentNamed(0xCD021293u)) {
            byDifficulty = nullptr;
            source->GetArray(0xCD021293u, &byDifficulty, 1);
            goal.m_text = byDifficulty->GetLocalString(static_cast<uint32_t>(m_difficulty));
        }
        if (source->ContainsComponentNamed(0xC4745838u)) {
            const char* name = reinterpret_cast<const char*>(0x006469B4); // ""
            source->GetString(0xC4745838u, &name, 1);
            goal.m_name = name;
        }
        if (source->ContainsComponentNamed(0xBD982FDCu)) {
            int ignored;
            source->GetInteger(0xBD982FDCu, &ignored, 1);
            goal.m_status = 0;
        }
        if (source->ContainsComponentNamed(0xD8A9D3CAu)) {
            byDifficulty = nullptr;
            source->GetArray(0xD8A9D3CAu, &byDifficulty, 1);
            goal.m_trick = byDifficulty->GetElement(static_cast<uint32_t>(m_difficulty));
        }
        if (source->ContainsComponentNamed(0x3C2FB496u)) {
            byDifficulty = nullptr;
            source->GetArray(0x3C2FB496u, &byDifficulty, 1);
            goal.m_value20 = byDifficulty->GetElement(static_cast<uint32_t>(m_difficulty));
        }
        goal.m_met = false;
    }
}

// 0x0051C950
bool CStatsManagerComponent::IsActive()
{
    if (!m_skater)
        return false;
    void* skate = SkateModule();
    if (!GameFunction<bool(__thiscall*)(void*, uint32_t)>(0x00553220)(GameVariable<void*>(0x007CE470), 0x1DED1EA4))
        return false;
    if (!m_activated || Field<uint32_t>(skate, 0x378) == 0xE92ECAFE)
        return false;
    // (Without a name the original compares what its slot held: this.)
    uint32_t name = reinterpret_cast<uint32_t>(this);
    reinterpret_cast<Script::CStruct*>(FirstProfile()->m_info)->GetChecksum(0xA1DC81F9u, &name, 1);
    return name == 0x0A7BE964;
}

// 0x0051C9D0
void CStatsManagerComponent::Set10(int a, int b, int)
{
    if (IsActive() && b != m_value14)
        m_value10 = a;
}

// 0x0051CA00
void CStatsManagerComponent::Reset()
{
    if (!IsActive())
        return;
    RunScript(0xD44EA68F);
    for (Goal& goal : m_goals)
        if (goal.m_status == 0 && goal.m_met)
            goal.m_met = false;
    m_airPhase = 0;
    m_jumpPhase = 0;
    m_countA80 = 0;
    m_countA84 = 0;
    m_jumpStart = m_jumpEnd;
    m_airStart = m_airEnd;
}

// 0x0051CAC0
void CStatsManagerComponent::SetValue58(float value)
{
    if (IsActive())
        m_value58 = value;
}

// 0x0051CAE0
void CStatsManagerComponent::Count(uint32_t name)
{
    if (!IsActive())
        return;
    if (name == 0xB9AE26D2)
        ++m_countA84;
    else if (name == 0xBA15FC7F)
        ++m_countA80;
}

// 0x0051CBF0
void CStatsManagerComponent::AnnounceMetGoals()
{
    if (!IsActive())
        return;
    CSkaterProfile* profile = FirstProfile();
    Script::CStruct* params = NewParams();
    bool announced = false;
    for (Goal& goal : m_goals) {
        if (goal.m_status != 0 || !goal.m_met)
            continue;
        if (!announced) {
            RunScript(0x750BCF49);
            announced = true;
        }
        uint32_t stat = goal.m_stat;
        goal.m_status = 1;
        // The stat's name (a global string per stat).
        uint32_t text;
        switch (stat) {
        case 0x6DCB497C: text = 0x585887CA; break;
        case 0x439F4704: text = 0xA523DF6C; break;
        case 0x9016B4E7: text = 0x1A6B74EA; break;
        case 0xAE798769: text = 0x1F1BCDEF; break;
        case 0x9B65D7B8: text = 0xA8386951; break;
        case 0xAF895B3F: text = 0xA17AC668; break;
        case 0xEDF5DB70: text = 0xB959CC23; break;
        case 0xB1FC0722: text = 0x069F2596; break;
        case 0xF0D90109: text = 0x1686EBD3; break;
        case 0xF73A13E3: text = 0x6E7A996D; break;
        default: text = 0; break;
        }
        if (text)
            params->AddString(0xC4745838u, Script::GetString(text));
        params->AddInteger(0x2044BAAFu, 1);
        RunScript(0x55E516C8, params);
        if (m_raiseStats)
            profile->SetStat(stat, profile->GetStat(stat) + 1);
    }
    if (announced) {
        Dbg::Printf(reinterpret_cast<const char*>(0x0064C2FC)); // "updating skater stats"
        reinterpret_cast<CSkater*>(m_skater)->SetStatsFromProfile(profile);
        if (!m_goalDone && GameFunction<bool(__thiscall*)(void*)>(0x00557E30)(GameFlow())) {
            RunScript(0xEF3A4EE5);
            m_goalDone = true;
        }
        RunScript(0x5A90FB86);
    }
    DeleteParams(params);
    m_countA80 = 0;
    m_countA84 = 0;
}

// 0x0051CE80
void CStatsManagerComponent::MeetGoal(int index)
{
    if (!IsActive())
        return;
    Script::CStruct* params = NewParams();
    Goal& goal = m_goals[index];
    params->AddString(0x61414D56u, goal.m_name);
    params->AddInteger(0xE288A7CBu, goal.m_target);
    if (goal.m_text)
        params->AddString(0xC257D69Au, goal.m_text);
    params->AddInteger(0x7F8C98FEu, index);
    RunScript(0x55E516C8, params);
    goal.m_met = true;
    DeleteParams(params);
}

// 0x0051CF90
void CStatsManagerComponent::Update()
{
    if (!IsActive())
        return;
    if (m_firstUpdate) {
        m_airPhase = 0;
        m_jumpPhase = 0;
        m_firstUpdate = false;
        return;
    }
    void* score = Score(m_skater);
    m_score = Truncate(Field<int>(score, 0x14) * static_cast<double>(Field<float>(score, 0x60)));
    float multiplier = Field<float>(score, 0x60);
    m_multiplier = multiplier;
    m_points = multiplier == 0.0f ? 0 : Truncate(m_score / static_cast<double>(multiplier));

    int physicsState = Field<int>(m_skaterState, 0x18);
    m_state = kStateOther;
    uint8_t* controlState = Field<uint8_t*>(m_physicsControl, 0x2C);
    const Mth::Vector& pos = m_object->m_pos;
    if (!Field<bool>(controlState, 0x118) && Field<int>(controlState, 0x1C) == 0) {
        if (Field<bool>(m_skaterState, 0x30)) {
            m_state = kStateLaunched;
            if (m_lastState != kStateLaunched) {
                m_airPhase = 1;
                m_airHeight = 0.0f;
                m_airStart = pos;
                m_airStartPoints = m_points;
                m_airStartMultiplier = m_multiplier;
                Dbg::Printf(reinterpret_cast<const char*>(0x0064C314), m_score); // "vert_start_score = %i"
                m_airBest = 0;
            } else {
                double height = static_cast<double>(pos.y) - m_airStart.y;
                if (height > m_airHeight)
                    m_airHeight = static_cast<float>(height);
                m_airScore = Truncate((static_cast<double>(multiplier) - m_airStartMultiplier) * Minus(m_points, m_airStartPoints));
                if (m_airScore >= m_airBest)
                    m_airBest = m_airScore;
            }
        } else if (m_lastState == kStateLaunched) {
            if (m_airPhase == 1) {
                m_airEnd = pos;
                double gained = static_cast<double>(m_multiplier) - m_airStartMultiplier;
                int airScore = Truncate(Minus(m_points, m_airStartPoints) * gained);
                m_airScore = airScore;
                m_airPhase = 2;
                if (gained > 1.0f && airScore != 0)
                    m_lastAirComboScore = airScore;
            }
            m_airScore = 0;
        } else {
            m_airPhase = 0;
            m_airHeight = 0.0f;
            m_airBest = 0;
        }

        if (physicsState == 1 && !Field<bool>(m_skaterState, 0x30)) {
            m_state = kStateAir;
            if (m_lastState != kStateAir && !Field<bool>(m_skaterState, 0x70)) {
                m_jumpPhase = 1;
                m_jumpStart = pos;
            }
        } else if (m_lastState == kStateAir) {
            if (m_jumpPhase == 1) {
                m_jumpEnd = pos;
                m_jumpPhase = 2;
            }
        } else {
            m_jumpPhase = 0;
        }
    } else {
        m_jumpPhase = 0;
    }

    // The open goals.
    bool met = false;
    for (int i = 0; i < kGoals; ++i) {
        Goal& goal = m_goals[i];
        if (goal.m_status != 0 || goal.m_met)
            continue;
        bool meets = false;
        switch (goal.m_kind) {
        case kRise:
            meets = m_jumpPhase == 2 &&
                    goal.m_target * static_cast<double>(kFeet) <= static_cast<double>(m_jumpEnd.y) - m_jumpStart.y;
            break;
        case kDrop:
            meets = m_jumpPhase == 2 &&
                    goal.m_target * static_cast<double>(kFeet) <= static_cast<double>(m_jumpStart.y) - m_jumpEnd.y;
            break;
        case kAirCombo:
            if (m_airPhase == 2 && m_airBest >= goal.m_target) {
                Script::CStruct* params = NewParams();
                params->AddInteger(0xCD66C8AEu, m_airBest);
                RunScript(0x7C9C7232, params);
                DeleteParams(params);
                meets = true;
            }
            break;
        case kAirHeight:
            meets = m_airPhase != 0 && goal.m_target * static_cast<double>(kFeet) <= m_airHeight;
            break;
        case kAirValue58:
            meets = m_airPhase != 0 && goal.m_target <= static_cast<double>(m_value58);
            break;
        case kComboScore:
            meets = m_score >= goal.m_target;
            break;
        case kMultiplier:
            meets = goal.m_target <= static_cast<double>(m_multiplier);
            break;
        case kCountA80:
            meets = m_countA80 >= goal.m_target;
            break;
        case kCountA84:
            meets = m_countA84 >= goal.m_target;
            break;
        case kTrickCount: {
            int target = goal.m_target;
            uint32_t trick = goal.m_trick;
            if (CountTricks(score, trick, 0, 0, static_cast<int>(goal.m_value20)) >= target) {
                meets = true;
            } else if (trick == kManual) {
                int manuals = CountTricks(score, trick, 0, 0, 1);
                meets = manuals + CountTricks(score, 0x89B7DBE4, 0, 0, 1) >= target;
            }
            break;
        }
        case kJumpDistance:
            if (m_jumpPhase == 2) {
                Mth::Vector start = {m_jumpStart.x, 0.0f, m_jumpStart.z, 1.0f};
                Mth::Vector end = {m_jumpEnd.x, 0.0f, m_jumpEnd.z, 1.0f};
                double distance = Distance(end, start);
                meets = goal.m_target * static_cast<double>(kFeet) <= distance;
            }
            break;
        case kAirDistance:
            if (m_airPhase == 2) {
                float target = static_cast<float>(goal.m_target);
                m_airEnd.y = 0.0f;
                m_airStart.y = 0.0f;
                double distance = Distance(m_airEnd, m_airStart);
                meets = target * static_cast<double>(kFeet) <= distance;
            }
            break;
        case kScore562370:
            // (it reads the goal's +0x18, the 0xCD021293 entry)
            meets = GameFunction<int(__thiscall*)(void*, uint32_t)>(0x00562370)(score, reinterpret_cast<uint32_t>(goal.m_text)) >=
                    goal.m_target;
            break;
        default:
            break;
        }
        if (meets) {
            MeetGoal(i);
            met = true;
        }
    }
    if (met)
        RunScript(0x41B03551);
    m_lastState = m_state;
    m_value58 = 0.0f;
    m_lastScore = m_score;
    m_lastPoints = m_points;
    m_lastMultiplier = m_multiplier;
}

// 0x0051D6A0
void CStatsManagerComponent::CheckBalanceTime(uint32_t trickType, float time, int)
{
    if (!IsActive())
        return;
    bool met = false;
    for (int i = 0; i < kGoals; ++i) {
        Goal& goal = m_goals[i];
        if (goal.m_status != 0 || goal.m_met)
            continue;
        int target = goal.m_target;
        uint32_t kind;
        if (trickType == kSlide || trickType == kGrind)
            kind = kGrindTime;
        else if (trickType == kNoseManual || trickType == kManual)
            kind = kManualTime;
        else if (trickType == kLip)
            kind = kLipTime;
        else
            continue;
        if (goal.m_kind != kind || !(target <= time))
            continue;
        MeetGoal(i);
        met = true;
    }
    if (met)
        RunScript(0x41B03551);
}

// --- member functions ---

// 0x0051C4A0
bool CStatsManagerComponent::ReInit(Script::CStruct* params, Script::CScript*)
{
    reinterpret_cast<void(__thiscall*)(CStatsManagerComponent*, Script::CStruct*)>(m_vtable[5])(this, params);
    return true;
}

// 0x0051CB20
bool CStatsManagerComponent::ResetGoals(Script::CStruct*, Script::CScript*)
{
    Reset();
    return true;
}

// 0x0051C4C0
bool CStatsManagerComponent::ActivateGoals(Script::CStruct*, Script::CScript*)
{
    m_activated = true;
    RunScript(0x7EC5F3E0);
    return true;
}

// 0x0051C4F0
bool CStatsManagerComponent::DeactivateGoals(Script::CStruct*, Script::CScript*)
{
    m_activated = false;
    RunScript(0x3CB06807);
    return true;
}

// 0x0051CB30
bool CStatsManagerComponent::GetGoalStatusMember(Script::CStruct* params, Script::CScript* script)
{
    int index;
    params->GetInteger(0u, &index, 1);
    Field<Script::CStruct*>(script, 0x14)->AddInteger(0x84FF9AE3u, m_goals[index].m_status);
    return true;
}

// 0x0051C520
bool CStatsManagerComponent::UnlockAmGoals(Script::CStruct*, Script::CScript*)
{
    for (Goal& goal : m_goals)
        if (goal.m_status == 2)
            goal.m_status = 0;
    return true;
}

// 0x0051C550
bool CStatsManagerComponent::UnlockProGoals(Script::CStruct*, Script::CScript*)
{
    for (Goal& goal : m_goals)
        if (goal.m_status == 3)
            goal.m_status = 0;
    return true;
}

// 0x0051CB70
bool CStatsManagerComponent::GetLastAirComboScore(Script::CStruct*, Script::CScript* script)
{
    Field<Script::CStruct*>(script, 0x14)->AddInteger(0x2D3B766Bu, m_lastAirComboScore);
    return true;
}

// 0x0051C580
bool CStatsManagerComponent::ClearLastAirComboScore(Script::CStruct*, Script::CScript*)
{
    m_lastAirComboScore = 0;
    return true;
}

} // namespace Obj

REPLACE(0x0051CB90, Obj::CStatsManagerComponent::Create);
REPLACE(0x0051C440, Obj::CStatsManagerComponent::RegisterMemberFunctions);
REPLACE(0x0051C5A0, &Obj::CStatsManagerComponent::Construct);
REPLACE(0x0051C460, &Obj::CStatsManagerComponent::DestroyBody);
REPLACE(0x0051C640, &Obj::CStatsManagerComponent::Destroy);
REPLACE(0x0051C470, &Obj::CStatsManagerComponent::GetGoalStatus);
REPLACE(0x0051C480, &Obj::CStatsManagerComponent::SetGoalStatus);
REPLACE(0x0051C670, &Obj::CStatsManagerComponent::InitFromStructure);
REPLACE(0x0051C950, &Obj::CStatsManagerComponent::IsActive);
REPLACE(0x0051C9D0, &Obj::CStatsManagerComponent::Set10);
REPLACE(0x0051CA00, &Obj::CStatsManagerComponent::Reset);
REPLACE(0x0051CAC0, &Obj::CStatsManagerComponent::SetValue58);
REPLACE(0x0051CAE0, &Obj::CStatsManagerComponent::Count);
REPLACE(0x0051CBF0, &Obj::CStatsManagerComponent::AnnounceMetGoals);
REPLACE(0x0051CE80, &Obj::CStatsManagerComponent::MeetGoal);
REPLACE(0x0051CF90, &Obj::CStatsManagerComponent::Update);
REPLACE(0x0051D6A0, &Obj::CStatsManagerComponent::CheckBalanceTime);
REPLACE(0x0051C4A0, &Obj::CStatsManagerComponent::ReInit);
REPLACE(0x0051CB20, &Obj::CStatsManagerComponent::ResetGoals);
REPLACE(0x0051C4C0, &Obj::CStatsManagerComponent::ActivateGoals);
REPLACE(0x0051C4F0, &Obj::CStatsManagerComponent::DeactivateGoals);
REPLACE(0x0051CB30, &Obj::CStatsManagerComponent::GetGoalStatusMember);
REPLACE(0x0051C520, &Obj::CStatsManagerComponent::UnlockAmGoals);
REPLACE(0x0051C550, &Obj::CStatsManagerComponent::UnlockProGoals);
REPLACE(0x0051CB70, &Obj::CStatsManagerComponent::GetLastAirComboScore);
REPLACE(0x0051C580, &Obj::CStatsManagerComponent::ClearLastAirComboScore);
