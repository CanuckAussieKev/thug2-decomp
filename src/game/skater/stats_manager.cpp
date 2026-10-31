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
#include "game/goals/goal_manager.h"
#include "game/skate/game_mode.h"
#include "game/skate/score.h"
#include "game/skate/skate_module.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

uint32_t g_statsManagerMemberFunctions[60]{
            0x82CE30CE, 0x4FF8DAC6, 0, 0, 0, 0,
            0x57879C96, 0x4FF8DAC6, 0, 0, 0, 0,
            0xEE59073C, 0x4FF8DAC6, 0, 0, 0, 0,
            0xF1745991, 0x4FF8DAC6, 0, 0, 0, 0,
            0x78E7D855, 0x4FF8DAC6, 0, 0, 0, 0,
            0xB341B5A5, 0x4FF8DAC6, 0, 0, 0, 0,
            0xB7FAC894, 0x4FF8DAC6, 0, 0, 0, 0,
            0x629D29FA, 0x4FF8DAC6, 0, 0, 0, 0,
            0xB1652DEC, 0x4FF8DAC6, 0, 0, 0, 0,
            0, 0x4FF8DAC6, 0, 0, 0, 0};

namespace {

extern void** const kVtable;

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

void LaunchScript(uint32_t name, Scripts::CStruct* params = nullptr) { Scripts::LaunchScript(name, params, nullptr, false, nullptr); }

Scripts::CStruct* NewParams()
{
    Scripts::CStruct* memory = Scripts::NewStruct();
    return memory ? memory->Init() : nullptr;
}
void DeleteParams(Scripts::CStruct* params)
{
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
}

void* SkateModule() { return static_cast<void*>(GameModules::g_skateModule); }
CRiderIdentity* FirstProfile() { return static_cast<GameModules::Skate*>(SkateModule())->GetProfile(static_cast<int>(0)); }
void* GameFlow() { return GoalSystem::GetObjectiveBoard(); }

// The skater's score component and its counts.
void* Score(CGameEntity* skater) { return reinterpret_cast<CRider*>(skater)->GetPointsTotal(); }
int CountTricks(void* score, uint32_t trick, int a, int b, int c)
{
    return static_cast<GameModules::Score*>(score)->CountTrickAfterTaps(trick, a, reinterpret_cast<bool&>(b), c);
}

// 0x00423680: the distance between two points (z, x, then y; unrounded).
double Distance(const Maths::Vector& a, const Maths::Vector& b)
{
    double dx = static_cast<double>(a.x) - b.x, dy = static_cast<double>(a.y) - b.y, dz = static_cast<double>(a.z) - b.z;
    return std::sqrt((dz * dz + dx * dx) + dy * dy);
}

// a - b, wrapping as the original's 32-bit subtraction.
int Minus(int a, int b) { return static_cast<int>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b)); }

// The original's float-to-int (_ftol: towards zero).
int Truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); } // (the low 32 bits)

} // namespace

// 0x0051CB90
CObjectModule* __cdecl CAttributeTracker::Create()
{
    void* memory = MemSys::AllocZeroed(sizeof(CAttributeTracker));
    return memory ? static_cast<CAttributeTracker*>(memory)->Construct() : nullptr;
}

// 0x0051C440
void __cdecl CAttributeTracker::PublishMethodTable()
{
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), g_statsManagerMemberFunctions, 0x18);
}

// 0x0051C5A0
CAttributeTracker* CAttributeTracker::Construct()
{
    CObjectModule::Construct();
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
void CAttributeTracker::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x0051C640
CAttributeTracker* CAttributeTracker::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x0051C470
int CAttributeTracker::GetGoalStatus(int index) { return m_goals[index].m_status; }

// 0x0051C480
void CAttributeTracker::SetGoalStatus(int index, int status)
{
    m_goals[index].m_status = status;
    if (status == 1)
        m_goalDone = true;
}

// 0x0051C670
void CAttributeTracker::ConfigureFromData(Scripts::CStruct*)
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
    m_skater = static_cast<CGameEntity*>(CompositeObjectManager()->LookUpEntity(m_object->m_id));
    if (!m_skater) {
        m_skaterState = nullptr;
        m_physicsControl = nullptr;
    } else {
        m_skaterState = m_skater->GetComponent(kSkaterState);
        m_physicsControl = m_skater->GetComponent(kSkaterPhysicsControl);
    }
    m_difficulty = static_cast<GoalSystem::CGoalManager*>(GameFlow())->GetSkillTier(true);
    DebugOut::Printf("STATSManager: difficulty ====================== %i\n", m_difficulty); // "STATSManager: difficulty ... %i"
    if (Scripts::FetchIntValue(0xB8688A8Au) == 1)
        m_raiseStats = true;

    Scripts::CArray* goals = Scripts::GetArray(0x4E46CC28u);
    int count = static_cast<int>(goals->m_size);
    for (int i = 0; i < count; ++i) {
        auto* source = reinterpret_cast<Scripts::CStruct*>(goals->GetElement(static_cast<uint32_t>(i)));
        Goal& goal = m_goals[i];
        uint32_t checksum;
        if (source->HasEntryNamed(0x0FFD7BBEu)) {
            source->ReadHashValue(0x0FFD7BBEu, &checksum, 1);
            goal.m_kind = checksum;
        }
        if (source->HasEntryNamed(0xC2455018u)) {
            source->ReadHashValue(0xC2455018u, &checksum, 1);
            goal.m_attrKey = checksum;
        }
        Scripts::CArray* byDifficulty;
        if (source->HasEntryNamed(0xE288A7CBu)) {
            byDifficulty = nullptr;
            source->GetArray(0xE288A7CBu, &byDifficulty, 1);
            goal.m_aimPoint = static_cast<int>(byDifficulty->GetElement(static_cast<uint32_t>(m_difficulty)));
        }
        if (source->HasEntryNamed(0xCD021293u)) {
            byDifficulty = nullptr;
            source->GetArray(0xCD021293u, &byDifficulty, 1);
            goal.m_text = byDifficulty->FindInlineText(static_cast<uint32_t>(m_difficulty));
        }
        if (source->HasEntryNamed(0xC4745838u)) {
            const char* name = ""; // ""
            source->GetString(0xC4745838u, &name, 1);
            goal.m_name = name;
        }
        if (source->HasEntryNamed(0xBD982FDCu)) {
            int ignored;
            source->FetchIntValue(0xBD982FDCu, &ignored, 1);
            goal.m_status = 0;
        }
        if (source->HasEntryNamed(0xD8A9D3CAu)) {
            byDifficulty = nullptr;
            source->GetArray(0xD8A9D3CAu, &byDifficulty, 1);
            goal.m_trick = byDifficulty->GetElement(static_cast<uint32_t>(m_difficulty));
        }
        if (source->HasEntryNamed(0x3C2FB496u)) {
            byDifficulty = nullptr;
            source->GetArray(0x3C2FB496u, &byDifficulty, 1);
            goal.m_value20 = byDifficulty->GetElement(static_cast<uint32_t>(m_difficulty));
        }
        goal.m_met = false;
    }
}

// 0x0051C950
bool CAttributeTracker::IsActive()
{
    if (!m_skater)
        return false;
    void* skate = SkateModule();
    if (!static_cast<GoalSystem::CMatchRules*>(static_cast<void*>(GoalSystem::g_gameMode))->IsTrue(0x1DED1EA4))
        return false;
    if (!m_activated || Field<uint32_t>(skate, 0x378) == 0xE92ECAFE)
        return false;
    // (Without a name the original compares what its slot held: this.)
    uint32_t name = reinterpret_cast<uint32_t>(this);
    reinterpret_cast<Scripts::CStruct*>(FirstProfile()->m_info)->ReadHashValue(0xA1DC81F9u, &name, 1);
    return name == 0x0A7BE964;
}

// 0x0051C9D0
void CAttributeTracker::Set10(int a, int b, int)
{
    if (IsActive() && b != m_value14)
        m_value10 = a;
}

// 0x0051CA00
void CAttributeTracker::Reset()
{
    if (!IsActive())
        return;
    LaunchScript(0xD44EA68F);
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
void CAttributeTracker::SetValue58(float value)
{
    if (IsActive())
        m_value58 = value;
}

// 0x0051CAE0
void CAttributeTracker::Count(uint32_t name)
{
    if (!IsActive())
        return;
    if (name == 0xB9AE26D2)
        ++m_countA84;
    else if (name == 0xBA15FC7F)
        ++m_countA80;
}

// 0x0051CBF0
void CAttributeTracker::AnnounceMetGoals()
{
    if (!IsActive())
        return;
    CRiderIdentity* profile = FirstProfile();
    Scripts::CStruct* params = NewParams();
    bool announced = false;
    for (Goal& goal : m_goals) {
        if (goal.m_status != 0 || !goal.m_met)
            continue;
        if (!announced) {
            LaunchScript(0x750BCF49);
            announced = true;
        }
        uint32_t stat = goal.m_attrKey;
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
            params->AddString(0xC4745838u, Scripts::GetString(text));
        params->PutInt(0x2044BAAFu, 1);
        LaunchScript(0x55E516C8, params);
        if (m_raiseStats)
            profile->SetAbilityLevel(stat, profile->GetStat(stat) + 1);
    }
    if (announced) {
        DebugOut::Printf("updating skater stats\n"); // "updating skater stats"
        reinterpret_cast<CRider*>(m_skater)->SetStatsFromProfile(profile);
        if (!m_goalDone && static_cast<GoalSystem::CGoalManager*>(GameFlow())->MayBeginObjective()) {
            LaunchScript(0xEF3A4EE5);
            m_goalDone = true;
        }
        LaunchScript(0x5A90FB86);
    }
    DeleteParams(params);
    m_countA80 = 0;
    m_countA84 = 0;
}

// 0x0051CE80
void CAttributeTracker::MeetGoal(int index)
{
    if (!IsActive())
        return;
    Scripts::CStruct* params = NewParams();
    Goal& goal = m_goals[index];
    params->AddString(0x61414D56u, goal.m_name);
    params->PutInt(0xE288A7CBu, goal.m_aimPoint);
    if (goal.m_text)
        params->AddString(0xC257D69Au, goal.m_text);
    params->PutInt(0x7F8C98FEu, index);
    LaunchScript(0x55E516C8, params);
    goal.m_met = true;
    DeleteParams(params);
}

// 0x0051CF90
void CAttributeTracker::Update()
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
    m_pointsHeld = Truncate(Field<int>(score, 0x14) * static_cast<double>(Field<float>(score, 0x60)));
    float multiplier = Field<float>(score, 0x60);
    m_multiplier = multiplier;
    m_pathVertices = multiplier == 0.0f ? 0 : Truncate(m_pointsHeld / static_cast<double>(multiplier));

    int physicsState = Field<int>(m_skaterState, 0x18);
    m_state = kStateOther;
    uint8_t* controlState = Field<uint8_t*>(m_physicsControl, 0x2C);
    const Maths::Vector& pos = m_object->m_pos;
    if (!Field<bool>(controlState, 0x118) && Field<int>(controlState, 0x1C) == 0) {
        if (Field<bool>(m_skaterState, 0x30)) {
            m_state = kStateLaunched;
            if (m_lastState != kStateLaunched) {
                m_airPhase = 1;
                m_airHeight = 0.0f;
                m_airStart = pos;
                m_airStartPoints = m_pathVertices;
                m_airStartMultiplier = m_multiplier;
                DebugOut::Printf("vert_start_score = %i\n", m_pointsHeld); // "vert_start_score = %i"
                m_airBest = 0;
            } else {
                double height = static_cast<double>(pos.y) - m_airStart.y;
                if (height > m_airHeight)
                    m_airHeight = static_cast<float>(height);
                m_airScore = Truncate((static_cast<double>(multiplier) - m_airStartMultiplier) * Minus(m_pathVertices, m_airStartPoints));
                if (m_airScore >= m_airBest)
                    m_airBest = m_airScore;
            }
        } else if (m_lastState == kStateLaunched) {
            if (m_airPhase == 1) {
                m_airEnd = pos;
                double gained = static_cast<double>(m_multiplier) - m_airStartMultiplier;
                int airScore = Truncate(Minus(m_pathVertices, m_airStartPoints) * gained);
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
                    goal.m_aimPoint * static_cast<double>(kFeet) <= static_cast<double>(m_jumpEnd.y) - m_jumpStart.y;
            break;
        case kDrop:
            meets = m_jumpPhase == 2 &&
                    goal.m_aimPoint * static_cast<double>(kFeet) <= static_cast<double>(m_jumpStart.y) - m_jumpEnd.y;
            break;
        case kAirCombo:
            if (m_airPhase == 2 && m_airBest >= goal.m_aimPoint) {
                Scripts::CStruct* params = NewParams();
                params->PutInt(0xCD66C8AEu, m_airBest);
                LaunchScript(0x7C9C7232, params);
                DeleteParams(params);
                meets = true;
            }
            break;
        case kAirHeight:
            meets = m_airPhase != 0 && goal.m_aimPoint * static_cast<double>(kFeet) <= m_airHeight;
            break;
        case kAirValue58:
            meets = m_airPhase != 0 && goal.m_aimPoint <= static_cast<double>(m_value58);
            break;
        case kComboScore:
            meets = m_pointsHeld >= goal.m_aimPoint;
            break;
        case kMultiplier:
            meets = goal.m_aimPoint <= static_cast<double>(m_multiplier);
            break;
        case kCountA80:
            meets = m_countA80 >= goal.m_aimPoint;
            break;
        case kCountA84:
            meets = m_countA84 >= goal.m_aimPoint;
            break;
        case kTrickCount: {
            int target = goal.m_aimPoint;
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
                Maths::Vector start = {m_jumpStart.x, 0.0f, m_jumpStart.z, 1.0f};
                Maths::Vector end = {m_jumpEnd.x, 0.0f, m_jumpEnd.z, 1.0f};
                double distance = Distance(end, start);
                meets = goal.m_aimPoint * static_cast<double>(kFeet) <= distance;
            }
            break;
        case kAirDistance:
            if (m_airPhase == 2) {
                float target = static_cast<float>(goal.m_aimPoint);
                m_airEnd.y = 0.0f;
                m_airStart.y = 0.0f;
                double distance = Distance(m_airEnd, m_airStart);
                meets = target * static_cast<double>(kFeet) <= distance;
            }
            break;
        case kScore562370:
            // (it reads the goal's +0x18, the 0xCD021293 entry)
            meets = static_cast<GameModules::Score*>(score)->CountTricksNamed(goal.m_text) >=
                    goal.m_aimPoint;
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
        LaunchScript(0x41B03551);
    m_lastState = m_state;
    m_value58 = 0.0f;
    m_lastScore = m_pointsHeld;
    m_lastPoints = m_pathVertices;
    m_lastMultiplier = m_multiplier;
}

// 0x0051D6A0
void CAttributeTracker::CheckBalanceTime(uint32_t trickType, float time, int)
{
    if (!IsActive())
        return;
    bool met = false;
    for (int i = 0; i < kGoals; ++i) {
        Goal& goal = m_goals[i];
        if (goal.m_status != 0 || goal.m_met)
            continue;
        int target = goal.m_aimPoint;
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
        LaunchScript(0x41B03551);
}

// --- member functions ---

// 0x0051C4A0
bool CAttributeTracker::ReInit(Scripts::CStruct* params, Scripts::CScript*)
{
    reinterpret_cast<void(__thiscall*)(CAttributeTracker*, Scripts::CStruct*)>(m_vtable[5])(this, params);
    return true;
}

// 0x0051CB20
bool CAttributeTracker::ResetGoals(Scripts::CStruct*, Scripts::CScript*)
{
    Reset();
    return true;
}

// 0x0051C4C0
bool CAttributeTracker::ActivateGoals(Scripts::CStruct*, Scripts::CScript*)
{
    m_activated = true;
    LaunchScript(0x7EC5F3E0);
    return true;
}

// 0x0051C4F0
bool CAttributeTracker::DeactivateGoals(Scripts::CStruct*, Scripts::CScript*)
{
    m_activated = false;
    LaunchScript(0x3CB06807);
    return true;
}

// 0x0051CB30
bool CAttributeTracker::GetGoalStatusMember(Scripts::CStruct* params, Scripts::CScript* script)
{
    int index;
    params->FetchIntValue(0u, &index, 1);
    Field<Scripts::CStruct*>(script, 0x14)->PutInt(0x84FF9AE3u, m_goals[index].m_status);
    return true;
}

// 0x0051C520
bool CAttributeTracker::UnlockAmGoals(Scripts::CStruct*, Scripts::CScript*)
{
    for (Goal& goal : m_goals)
        if (goal.m_status == 2)
            goal.m_status = 0;
    return true;
}

// 0x0051C550
bool CAttributeTracker::UnlockProGoals(Scripts::CStruct*, Scripts::CScript*)
{
    for (Goal& goal : m_goals)
        if (goal.m_status == 3)
            goal.m_status = 0;
    return true;
}

// 0x0051CB70
bool CAttributeTracker::GetLastAirComboScore(Scripts::CStruct*, Scripts::CScript* script)
{
    Field<Scripts::CStruct*>(script, 0x14)->PutInt(0x2D3B766Bu, m_lastAirComboScore);
    return true;
}

// 0x0051C580
bool CAttributeTracker::ClearLastAirComboScore(Scripts::CStruct*, Scripts::CScript*)
{
    m_lastAirComboScore = 0;
    return true;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CAttributeTracker::Destroy,
         &GameObjects::CAttributeTracker::Update,
         &GameObjects::CAttributeTracker::ConfigureFromData,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &GameObjects::CObjectModule::RunMemberFunction,
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
