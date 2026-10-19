// CREATE 100% CAS's script commands (game/front/career_complete.h).

#include "game/front/career_complete.h"

#include <cstdint>

#include "game/core/crc.h"
#include "game/obj/object.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
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
constexpr uint32_t kCareer = 0x4DA4937B;    // career (a story goal; classic ones: 0x4C10DE52)

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
    auto* skater = GameFunction<Obj::CCompositeObject*(__thiscall*)(void*)>(0x00566520)(Skate()); // (the local one)
    auto* stats = skater ? reinterpret_cast<uint8_t*>(skater->GetComponent(0x4FF8DAC6)) : nullptr;
    if (!stats)
        return 0;
    for (int i = 0; i < kStatGoals; ++i) {
        *reinterpret_cast<int*>(stats + kStatGoalStatus + i * kStatGoalSize) = 1;
        stats[kStatGoalJustDone + i * kStatGoalSize] = 0;
    }
    return kStatGoals;
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
    int goalCount = 0;
    for (uint32_t i = 0; i < levels->m_size; ++i) {
        auto* level = reinterpret_cast<Script::CStruct*>(levels->GetElement(i));
        int number = -1, points = 0, total = 0;
        if (!level || !level->GetInteger(Checksum("level"), &number, 0) || number < 0 ||
            number >= Obj::CSkaterCareer::kLevels)
            continue;
        level->GetInteger(Checksum("points"), &points, 0);
        level->GetInteger(Checksum("total"), &total, 0);
        // As AddCash (0x005845C0) and SetLevelAmount (0x005845F0) change them.
        career->m_totalCash = static_cast<uint16_t>(career->m_totalCash + (points - career->m_cash[number]));
        career->m_cash[number] = static_cast<uint16_t>(points);
        career->m_totalAmount = static_cast<uint16_t>(career->m_totalAmount + (total - career->m_amount[number]));
        career->m_amount[number] = static_cast<uint16_t>(total);
        Script::CArray* goals = nullptr;
        if (!level->GetArray(Checksum("goals"), &goals, 0) || !goals)
            continue;
        for (uint32_t g = 0; g < goals->m_size; ++g) {
            Script::CStruct* memory = Script::NewStruct();
            Script::CStruct* goal = memory ? memory->Init() : nullptr;
            if (!goal)
                break;
            goal->AddInteger(kLevelNum, number);
            goal->AddInteger(kUnlocked, 1);
            goal->AddInteger(k2206B1E7, 0);
            goal->AddInteger(kHasBeaten, 1);
            goal->AddInteger(kRecord, 0);
            goal->AddChecksum(0u, kCareer);
            saved->AddStructure(goals->GetElement(g), goal); // (a copy)
            goal->Clear();
            Script::DeleteStruct(goal);
            ++goalCount;
        }
    }
    int statGoals = CompleteStatGoals();
    Log("create 100%% cas: %u levels, %d goals beaten; story points %u / %u; %d stat challenges done", levels->m_size,
        goalCount, career->m_totalCash, career->m_totalAmount, statGoals);
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
