// CREATE 100% CAS's script commands (game/front/career_complete.h).

#include "game/front/career_complete.h"

#include <cstdint>

#include "game/core/crc.h"
#include "game/obj/object.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skate/gap_checklist.h"
#include "game/skate/skate_module.h"
#include "game/skater/skater.h"
#include "hook/game.h"
#include "hook/log.h"

namespace Front::CareerComplete {

namespace {

uint32_t Checksum(const char* name) { return Crc::GenerateCRCFromString(name); }

// A goal's saved state as the goal manager keeps it (the names a goal's save writes - 0x00554770).
constexpr uint32_t kLevelNum = 0x1A4CDE06;  // levelNum
constexpr uint32_t kUnlocked = 0xD3E93882;
constexpr uint32_t k2206B1E7 = 0x2206B1E7;
constexpr uint32_t kHasBeaten = 0x49807745; // hasBeaten
constexpr uint32_t kRecord = 0xC22A2B72;
// The bare name a goal's save adds: career for a story goal, classic for a classic mode one (its classic_mode_goal
// test - 0x00554770); GoalManager_ResetCareer <mode> (0x00557E70) drops the store's goals of a mode by it.
constexpr uint32_t kCareer = 0x4DA4937B;    // career
constexpr uint32_t kClassic = 0x4C10DE52;   // classic

uint8_t* Skate() { return GameVariable<uint8_t*>(0x007CE478); }

// The local skater's stats manager (component 0x4FF8DAC6, the skater's story stat challenges - stats_goals): its 70
// challenges from +0x1C, 0x24 bytes each, the status at +0x10 of each (0 open, 1 done - the stat point given -,
// 2 / 3 locked until StatsManager_UnlockAmGoals / UnlockProGoals; StatsManager_GetGoalStatus 0x0051CB30 reads it,
// the stats menu counts the done ones: 3 + done = the stat's maximum) and the "just done" byte at +0x0C.
constexpr int kStatGoals = 70;
constexpr int kStatGoalSize = 0x24;
constexpr int kStatGoalStatus = 0x1C + 0x10;
constexpr int kStatGoalJustDone = 0x1C + 0x0C;

int CompleteStatGoals()
{
    Obj::CCompositeObject* skater = reinterpret_cast<Mdl::Skate*>(Skate())->GetLocalSkater(); // (the local one)
    auto* stats = skater ? reinterpret_cast<uint8_t*>(skater->GetComponent(0x4FF8DAC6)) : nullptr;
    if (!stats)
        return 0;
    for (int i = 0; i < kStatGoals; ++i) {
        *reinterpret_cast<int*>(stats + kStatGoalStatus + i * kStatGoalSize) = 1;
        stats[kStatGoalJustDone + i * kStatGoalSize] = 0;
    }
    return kStatGoals;
}

// One goal put into the goal manager's store of goals that aren't loaded (+0x14) as beaten, as a goal's save writes
// it, with the bare name `mode`. A record already there (a classic score goal's best) is kept.
void StoreBeatenGoal(Script::CStruct* saved, uint32_t name, int level, uint32_t mode)
{
    int record = 0;
    Script::CStruct* old = nullptr;
    if (saved->GetStructure(name, &old, 0) && old)
        old->GetInteger(kRecord, &record, 0);
    Script::CStruct* memory = Script::NewStruct();
    Script::CStruct* goal = memory ? memory->Init() : nullptr;
    if (!goal)
        return;
    goal->AddInteger(kLevelNum, level);
    goal->AddInteger(kUnlocked, 1);
    goal->AddInteger(k2206B1E7, 0);
    goal->AddInteger(kHasBeaten, 1);
    goal->AddInteger(kRecord, record);
    goal->AddChecksum(0u, mode);
    saved->AddStructure(name, goal); // (a copy; replaces one of the same name)
    goal->Clear();
    Script::DeleteStruct(goal);
}

// Every gap of `gaps` ({name score [pos]} each) put in the level's checklist and landed once, and the level visited
// (CSkaterCareer::AllGapsGot 0x00584BC0 asks for both). A landed gap keeps where the skater started it, which the
// pause menu's VIEW GAPS puts its camera above (gap_menu_focus_show_gap); ours get `pos` (where the gap ends: the
// node leading to its EndGap) facing along z - a gap with neither stays at the origin, but never with a zero
// direction. Gaps already landed keep their count and positions. Returns the checklist's gap count.
int FillGapChecklist(Obj::CSkaterCareer* career, int level, Script::CArray* gaps)
{
    Game::CGapChecklist* checklist = career->m_gaps[level];
    if (!checklist)
        return 0;
    const Mth::Vector facing = {0.0f, 0.0f, 1.0f, 0.0f};
    for (uint32_t i = 0; i < gaps->m_size; ++i) {
        auto* gap = reinterpret_cast<Script::CStruct*>(gaps->GetElement(i));
        const char* name = nullptr;
        int score = 0;
        if (!gap || !gap->GetString(Checksum("name"), &name, 0) || !name || !*name ||
            !gap->GetInteger(Checksum("score"), &score, 0) || !score)
            continue;
        int index = checklist->FindGap(name);
        if (index >= 0 && checklist->GetGapCount(index) > 0)
            continue; // (landed already)
        checklist->AddGap(name, 1, score); // (also marks the checklist built; one already there is left as it is)
        Script::VectorValue pos = {0.0f, 0.0f, 0.0f, 1.0f};
        gap->GetVector(Checksum("pos"), &pos, 0);
        checklist->SetGapPositions(name, {pos.x, pos.y, pos.z, 1.0f}, facing);
    }
    int count = checklist->Count();
    for (int i = 0; i < count; ++i) {
        Game::CGapCheck* gap = checklist->GetGap(i);
        if (gap && !gap->m_count) {
            gap->m_count = 1;
            gap->m_skaterStartDir = facing;
        }
    }
    career->m_visited[level] = true;
    return count;
}

} // namespace

bool& Enabled()
{
    static bool on = ConfigInt("features", "create_100_cas", 1) != 0;
    return on;
}

bool __cdecl CFunc_Available(Script::CStruct*, Script::CScript*) { return Enabled(); }

bool __cdecl CFunc_Apply(Script::CStruct* params, Script::CScript*)
{
    if (!Enabled()) {
        Log("create 100%% cas: off ([features] create_100_cas=0)");
        return false;
    }
    uint8_t* skate = Skate();
    Script::CArray* levels = nullptr;
    if (!skate || !params || !params->GetArray(Checksum("levels"), &levels, 0) || !levels)
        return false;
    auto* career = *reinterpret_cast<Obj::CSkaterCareer**>(skate + 0x20);
    uint8_t* goalManager = *reinterpret_cast<uint8_t**>(skate + 0x38C);
    Script::CStruct* saved = goalManager ? *reinterpret_cast<Script::CStruct**>(goalManager + 0x14) : nullptr;
    if (!career || !saved)
        return false;
    int goalCount = 0, classicCount = 0, gapLevels = 0, gapCount = 0;
    for (uint32_t i = 0; i < levels->m_size; ++i) {
        auto* level = reinterpret_cast<Script::CStruct*>(levels->GetElement(i));
        int number = -1;
        if (!level || !level->GetInteger(Checksum("level"), &number, 0) || number < 0 ||
            number >= Obj::CSkaterCareer::kLevels)
            continue;
        // The story's points and potential, as AddCash (0x005845C0) and SetLevelAmount (0x005845F0) change them.
        int points = 0, total = 0;
        if (level->GetInteger(Checksum("points"), &points, 0)) {
            level->GetInteger(Checksum("total"), &total, 0);
            career->m_totalCash = static_cast<uint16_t>(career->m_totalCash + (points - career->m_cash[number]));
            career->m_cash[number] = static_cast<uint16_t>(points);
            career->m_totalAmount =
                static_cast<uint16_t>(career->m_totalAmount + (total - career->m_amount[number]));
            career->m_amount[number] = static_cast<uint16_t>(total);
        }
        Script::CArray* goals = nullptr;
        if (level->GetArray(Checksum("goals"), &goals, 0) && goals) {
            for (uint32_t g = 0; g < goals->m_size; ++g, ++goalCount)
                StoreBeatenGoal(saved, goals->GetElement(g), number, kCareer);
        }
        // Classic mode: its goals beaten, and the level's classic goal counter at their number - what each classic
        // goal's win adds (the goal win 0x005576D0 calls IncrementCounter 0x00584620 in classic mode);
        // GetClassicLevelGoalCount 0x005AEAE0 reads it for the level select and GAME PROGRESS.
        Script::CArray* classic = nullptr;
        if (level->GetArray(Checksum("classic_goals"), &classic, 0) && classic) {
            for (uint32_t g = 0; g < classic->m_size; ++g, ++classicCount)
                StoreBeatenGoal(saved, classic->GetElement(g), number, kClassic);
            if (number < static_cast<int>(sizeof(career->m_counter)))
                career->m_counter[number] = static_cast<uint8_t>(classic->m_size);
        }
        Script::CArray* gaps = nullptr;
        if (level->GetArray(Checksum("gaps"), &gaps, 0) && gaps) {
            gapCount += FillGapChecklist(career, number, gaps);
            ++gapLevels;
        }
    }
    // The classic total complete as the counters add up (the total possible, +0x5EF, is SetClassicTotalGoals').
    int counterTotal = 0;
    for (uint8_t counter : career->m_counter)
        counterTotal += counter;
    career->m_totalCounter = static_cast<uint8_t>(counterTotal);
    int statGoals = CompleteStatGoals();
    Log("create 100%% cas: %u levels; %d story goals beaten, story points %u / %u; %d classic goals beaten, classic "
        "goals %u / %u; %d gaps got in %d levels, all gaps got: %d; %d stat challenges done",
        levels->m_size, goalCount, career->m_totalCash, career->m_totalAmount, classicCount, career->m_totalCounter,
        career->m_flag5EF, gapCount, gapLevels, career->AllGapsGot() ? 1 : 0, statGoals);
    return true;
}

void RegisterScriptFunctions()
{
    static const Script::CFunctionEntry functions[] = {
        {"Career100_Available", reinterpret_cast<void*>(&CFunc_Available)},
        {"Career100_Apply", reinterpret_cast<void*>(&CFunc_Apply)},
    };
    Script::RegisterCFunctions(functions, sizeof(functions) / sizeof(functions[0]));
}

} // namespace Front::CareerComplete
