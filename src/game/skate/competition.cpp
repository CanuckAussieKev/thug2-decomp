// Mdl::CCompetition: 0x0054FFD0-0x005517BF (see competition.h).

#include "game/skate/competition.h"

#include <new>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/mth/random.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/profile_manager.h"
#include "game/goals/goal_manager.h"
#include "game/script/parse.h"
#include "game/obj/anim_movies.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/gfx/tod_manager.h"
#include "game/gfx/loading_bar.h"

namespace Mdl {

GAME_GLOBAL(float, g_competitionTiny, 0x006453D4, 0.0001f);
GAME_GLOBAL(float, g_competitionTwo, 0x00645448, 2.0f);
GAME_GLOBAL(float, g_competitionThird, 0x006463D8, 0.33333334f);
GAME_GLOBAL(float, g_competitionThree, 0x006467A8, 3.0f);
GAME_GLOBAL(float, g_competitionEighth, 0x0064B960, 0.125f);
GAME_GLOBAL(float, g_competitionNearlyHalf, 0x0064D84C, 0.499999f);
GAME_GLOBAL(float, g_competitionNinetyNine, 0x0064D950, 99.0f);
GAME_GLOBAL(float, g_competitionOverHalf, 0x0064D954, 0.500099f);
GAME_GLOBAL(float, g_competitionMaxJudge, 0x0064D958, 99.85f);
GAME_GLOBAL(float, g_competitionMaxPoints, 0x0064D95C, 99.9f);

namespace {

constexpr uint32_t kNoName = 0;                    // (an unnamed component: a flag)
constexpr uint32_t kLeaderBoardNames = 0x1CC6F48D;  // leader_board_names
constexpr uint32_t kFirstPlaceName = 0xFAF19012;    // first_place_name
constexpr uint32_t kName = 0xA1DC81F9;              // Name
constexpr uint32_t kScore = 0xCD66C8AE;             // score
constexpr uint32_t kPlayer = 0x67E6859A;            // player
constexpr uint32_t kTopJudge = 0x7B1B959F;          // top_judge
constexpr uint32_t kLeaderBoard = 0x370B9420;       // leader_board
constexpr uint32_t kAddLeaderBoard = 0xCE76F8C1;    // goal_comp_add_leader_board
constexpr uint32_t kJudgeNames[CCompetition::kJudges] = {
    0x84F93830, 0x1DF0698A, 0x6AF7591C, 0xF493CCBF, 0x8394FC29, // comp_str_Judge1..5
};

// The constants, as the original reads them.
float Tenth() { return Script::g_parseNudge; }        // 0.1
float Eighth() { return g_competitionEighth; }       // 0.125
float Two() { return g_competitionTwo; }          // 2.0
float NearlyHalf() { return g_competitionNearlyHalf; }   // 0.4999990 (rounding to the nearest)
float Hundred() { return Gfx::g_hundred; }      // 100
float MaxPoints() { return g_competitionMaxPoints; }    // 99.9
float Half() { return Gfx::g_half; }         // 0.5
float Zero() { return Obj::g_floatZero; }         // 0
float MaxJudge() { return g_competitionMaxJudge; }     // 99.85
float Three() { return g_competitionThree; }        // 3
float Ten() { return Nx::g_floatTen; }          // 10
float Tiny() { return g_competitionTiny; }         // 0.0001
float OverHalf() { return g_competitionOverHalf; }     // 0.500099
float NinetyNine() { return g_competitionNinetyNine; }   // 99
float Third() { return g_competitionThird; }        // 1/3
constexpr float kScoreSpread = 1.9f;                             // (0x3FF33333, pushed as an immediate)

// The x87 code's conversions: truncation toward zero (_ftol).
int Truncate(double value) { return static_cast<int>(value); }
// A score rounded to the nearest (the original adds 0.499999 and truncates).
int Round(float value) { return Truncate(static_cast<double>(value) + NearlyHalf()); }

Obj::CPlayerProfileManager* ProfileManager() { return Obj::g_profileManager; }
// The profile manager's template at `index` (0x00557FB0) and its count (0x00469490: the table's).
Obj::CSkaterProfile* TemplateAt(Obj::CPlayerProfileManager* manager, int index)
{
    // (0x00557FB0 is the lookup table's by-index code, shared with the goal manager's: its first member is the table too)
    return reinterpret_cast<Obj::CSkaterProfile*>(reinterpret_cast<Game::CGoalManager*>(manager)->GetGoalByIndex(index));
}

// The goal manager (0x005591B0) and its active competition goal (0x00558D90; null: none).
bool CompetitionGoalActive()
{
    return Game::GetGoalManager()->GetActiveCompetition() != nullptr;
}

Script::CStruct* NewStructure()
{
    Script::CStruct* structure = Script::NewStruct();
    return structure ? structure->Init() : nullptr;
}
void DeleteStructure(Script::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Script::DeleteStruct(structure);
    }
}
Script::CArray* NewArray()
{
    Script::CArray* array = Script::NewArray();
    return array ? array->Init() : nullptr;
}
// An array handed to a structure (which copied it): its contents and itself freed.
void FreeArray(Script::CArray* array)
{
    Script::CleanUpArray(array);
    if (array) {
        array->Clear();
        Script::DeleteArray(array);
    }
}

} // namespace

int CCompetition::Leader::BestTwo(int count) const
{
    int best = 0;
    int second = 0;
    for (int i = 0; i < count; ++i) {
        int score = m_scores[i];
        if (score > best) {
            second = best;
            best = score;
        } else if (score > second) {
            second = score;
        }
    }
    return best + second;
}

int CCompetition::Leader::BestTwoOfFourWords() const
{
    int best = 0;
    int second = 0;
    for (int i = 0; i < 4; ++i) {
        int score = Word(i);
        if (score > best) {
            second = best;
            best = score;
        } else if (score > second) {
            second = score;
        }
    }
    return best + second;
}

// 0x0054FFD0
CCompetition::Leader* CCompetition::Leader::Construct()
{
    new (&m_name) Str::String();
    return this;
}

// 0x0054FFF0
void CCompetition::DestroyBody()
{
    // (the compiler's array-destructor helper, 0x0062382B: last to first)
    for (int i = 7; i >= 0; --i)
        m_leaders[i].m_name.~String();
}

// 0x00550010
void CCompetition::EditParams(Script::CStruct* params) { m_params->AppendStructureIfAny(params); }

// 0x00550020
void CCompetition::StartRun()
{
    ++m_runs;
    m_points = 0.0f;
    m_bailPoints = 0.0f;
}

// 0x00550040
bool CCompetition::IsTopJudge(int judge)
{
    // The judges by score, highest first (a selection sort; equal scores keep their order).
    int order[kJudges] = {0, 1, 2, 3, 4};
    for (int i = 0; i < kJudges; ++i) {
        for (int j = i; j < kJudges; ++j) {
            if (m_judgeScores[order[i]] < m_judgeScores[order[j]]) {
                int swap = order[i];
                order[i] = order[j];
                order[j] = swap;
            }
        }
    }
    return order[0] == judge || order[1] == judge || order[2] == judge;
}

// 0x005501A0
bool CCompetition::PlaceIs(int place) { return place == m_place; }

// 0x005501C0
bool CCompetition::RoundIs(int round) { return round == m_runs + 1; }

// 0x005501E0
void CCompetition::End() { m_ended = true; }

// 0x005501F0
bool CCompetition::IsEnded() { return m_ended; }

// 0x00550200
void CCompetition::SortLeaders()
{
    int count = (m_ended ? 2 : m_runs) + 1;
    for (int i = 0; i < kLeaders - 1; ++i) {
        int best = i;
        for (int j = i + 1; j < kLeaders; ++j) {
            int score = m_leaders[j].BestTwo(count);
            int bestScore = m_leaders[best].BestTwo(count);
            if (score > bestScore || (m_leaders[j].m_id == 0 && score == bestScore))
                best = j;
        }
        if (best != i) {
            // (a leader's copy: the name, the id, the scores and m_unknown1C - not the padding)
            Leader& a = m_leaders[best];
            Leader& b = m_leaders[i];
            Str::String name(a.m_name);
            int id = a.m_id;
            int scores[kRuns] = {a.m_scores[0], a.m_scores[1], a.m_scores[2]};
            bool unknown = a.m_unknown1C;
            a.m_name = b.m_name;
            a.m_id = b.m_id;
            for (int k = 0; k < kRuns; ++k)
                a.m_scores[k] = b.m_scores[k];
            a.m_unknown1C = b.m_unknown1C;
            b.m_name = name;
            b.m_id = id;
            for (int k = 0; k < kRuns; ++k)
                b.m_scores[k] = scores[k];
            b.m_unknown1C = unknown;
        }
    }
}

// 0x005504B0
CCompetition* CCompetition::Construct(int)
{
    // (the compiler's array-constructor helper, 0x0062388B)
    for (Leader& leader : m_leaders)
        leader.Construct();
    m_params = NewStructure();
    return this;
}

// 0x00550560
void CCompetition::Start(float bronze, float silver, float gold, float bronzeScore, float silverScore,
                         float goldScore, float bailPenalty)
{
    m_bronze = bronze;
    m_silver = silver;
    m_gold = gold;
    m_bronzeScore = bronzeScore;
    m_silverScore = silverScore;
    m_goldScore = goldScore;
    m_bailPenalty = bailPenalty;
    m_ended = false;

    Obj::CPlayerProfileManager* manager = ProfileManager();
    m_leaders[0].m_name = manager->GetCurrentProfile()->GetDisplayName();

    // The pros that can compete: not hidden (0xF3149AC5) and not the player, in a random order.
    int pros[96]; // (the original's stack room)
    int proCount = 0;
    int templates = static_cast<int>(manager->m_templates.m_count);
    for (int i = 0; i < templates; ++i) {
        Obj::CSkaterProfile* profile = TemplateAt(manager, i);
        if (profile->IsPro() && !profile->HasFlagF3149AC5() &&
            Crc::GenerateCRCFromString(profile->GetDisplayName()) !=
                Crc::GenerateCRCFromString(m_leaders[0].m_name.c_str()))
            pros[proCount++] = i;
    }
    for (int i = 0; i < proCount; ++i) {
        int other = Mth::Rnd2(proCount);
        int swap = pros[i];
        pros[i] = pros[other];
        pros[other] = swap;
    }
    int ids[kLeaders - 1] = {1, 2, 3, 4, 5, 6, 7};
    for (int i = 0; i < kLeaders - 1; ++i) {
        int other = Mth::Rnd2(kLeaders - 1);
        int swap = ids[i];
        ids[i] = ids[other];
        ids[other] = swap;
    }

    m_leaders[0].m_id = 0;
    for (int& score : m_leaders[0].m_scores)
        score = 0;
    bronze = static_cast<float>(Mth::PlusOrMinusExact(Two()) + bronze);
    silver = static_cast<float>(Mth::PlusOrMinusExact(Two()) + silver);
    gold = static_cast<float>(Mth::PlusOrMinusExact(Two()) + gold);
    if (!(silver < gold))
        silver = static_cast<float>(static_cast<double>(gold) - Tenth());
    if (!(bronze < silver))
        bronze = static_cast<float>(static_cast<double>(silver) - Tenth());

    for (int i = 0; i < kLeaders - 1; ++i) {
        Leader& leader = m_leaders[i + 1];
        leader.m_id = ids[i];
        Script::CArray* names;
        const char* name;
        if (m_params->GetArray(kLeaderBoardNames, &names, 0))
            name = names->GetLocalString(static_cast<uint32_t>(i));
        else
            name = manager->GetTemplate(manager->GetTemplateName(pros[i]))->GetDisplayName();
        leader.m_name = name;

        // The first three pros near the medal points, the others near the bronze.
        float points;
        if (i == 0) {
            points = gold;
        } else if (i == 1) {
            points = silver;
        } else if (i == 2) {
            points = bronze;
        } else {
            float spread = static_cast<float>(static_cast<double>(bronze) * Eighth());
            double random = Mth::PlusOrMinusExact(spread);
            points = static_cast<float>(static_cast<double>(bronze) - spread + random);
            if (!(points < bronze))
                points = static_cast<float>(static_cast<double>(bronze) - Tenth());
        }
        // Two judges' scores either side of the points, the third a little lower.
        float high = static_cast<float>(Mth::PlusOrMinusExact(Two()) + points);
        float low = high;
        float mirrored = static_cast<float>(static_cast<double>(points) + points - high);
        if (mirrored < high)
            low = mirrored;
        int first = Mth::Rnd2(3);
        int second;
        do {
            second = Mth::Rnd2(3);
        } while (second == first);
        float base = low - Two();
        for (int& score : leader.m_scores)
            score = Truncate(Mth::PlusOrMinusExact(kScoreSpread) + base + NearlyHalf());
        leader.m_scores[first] = Round(high);
        leader.m_scores[second] = Round(mirrored);
    }

    Dbg::Printf(reinterpret_cast<const char*>(0x0064D888));
    for (int i = 0; i < kLeaders; ++i) {
        Leader& leader = m_leaders[i];
        Dbg::Printf(reinterpret_cast<const char*>(0x0064D85C), i, leader.m_id, leader.m_name.c_str(), leader.Word(0),
                    leader.Word(1), leader.Word(2), leader.BestTwoOfFourWords());
    }
    SortLeaders();
    const char* firstPlace;
    if (m_params->GetString(kFirstPlaceName, &firstPlace, 0))
        m_leaders[0].m_name = firstPlace;
    Dbg::Printf(reinterpret_cast<const char*>(0x0064D850));
    for (int i = 0; i < kLeaders; ++i) {
        Leader& leader = m_leaders[i];
        Dbg::Printf(reinterpret_cast<const char*>(0x0064D85C), i, leader.m_id, leader.m_name.c_str(), leader.Word(0),
                    leader.Word(1), leader.Word(2), leader.BestTwoOfFourWords());
    }
    for (int i = 0; i < kJudges; ++i)
        m_judgeNames[i] = Script::GetString(kJudgeNames[i]);
    m_runs = -1;
}

// 0x00550B50
void CCompetition::ShowLeaderBoard()
{
    if (!CompetitionGoalActive())
        return;
    Script::CStruct* params = NewStructure();
    Script::CArray* board = NewArray();
    board->SetSizeAndType(5, Script::ESYMBOLTYPE_STRUCTURE);
    Dbg::Printf(reinterpret_cast<const char*>(0x0064D8A4));
    for (int i = 0; i < kLeaders; ++i) {
        Leader& leader = m_leaders[i];
        Dbg::Printf(reinterpret_cast<const char*>(0x0064D85C), i, leader.m_id, leader.m_name.c_str(), leader.Word(0),
                    leader.Word(1), leader.Word(2), leader.BestTwoOfFourWords());
    }
    for (int i = 0; i < 5; ++i) {
        Leader& leader = m_leaders[i];
        Script::CStruct* entry = NewStructure();
        entry->AddString(kName, leader.m_name.c_str());
        Dbg::Printf(reinterpret_cast<const char*>(0x0064D88C), m_runs);
        entry->AddInteger(kScore, leader.BestTwo(m_runs + 1));
        if (leader.m_id == 0)
            entry->AddChecksum(kNoName, kPlayer);
        board->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(entry));
    }
    params->AddArray(kLeaderBoard, board);
    FreeArray(board);
    Script::SpawnScript(kAddLeaderBoard, params, 0, nullptr, 0, 0, false, false, false, false);
    DeleteStructure(params);
}

// 0x00550EF0
void CCompetition::EndRun(int score)
{
    if (m_ended)
        return;
    Dbg::Printf(reinterpret_cast<const char*>(0x0064D9C8), score);
    Dbg::Printf(reinterpret_cast<const char*>(0x0064D9A8), static_cast<double>(m_bronzeScore),
                static_cast<double>(m_silverScore), static_cast<double>(m_goldScore));

    // The points: linear between the medal scores (above gold: towards 100 at twice the gold score).
    double x = score;
    float points;
    if (x < m_bronzeScore)
        points = static_cast<float>(static_cast<double>(m_bronze) / m_bronzeScore * x);
    else if (x < m_silverScore)
        points = static_cast<float>((static_cast<double>(m_silver) - m_bronze) * (x - m_bronzeScore) /
                                        (static_cast<double>(m_silverScore) - m_bronzeScore) +
                                    m_bronze);
    else if (x < m_goldScore)
        points = static_cast<float>((static_cast<double>(m_gold) - m_silver) * (x - m_silverScore) /
                                        (static_cast<double>(m_goldScore) - m_silverScore) +
                                    m_silver);
    else
        points = static_cast<float>((x - m_goldScore) * (static_cast<double>(Hundred()) - m_gold) /
                                        (static_cast<double>(m_goldScore) + m_goldScore - m_goldScore) +
                                    m_gold);
    Dbg::Printf(reinterpret_cast<const char*>(0x0064D98C), static_cast<double>(points));
    points = points - m_bailPoints;
    Dbg::Printf(reinterpret_cast<const char*>(0x0064D960), static_cast<double>(m_bailPoints),
                static_cast<double>(points));

    float max = MaxPoints();
    m_points = points;
    if (!(points <= max))
        m_points = max;
    float spread = static_cast<float>((static_cast<double>(max) - m_points) * Tenth());
    m_points = static_cast<float>(
        Mth::PlusOrMinusExact(static_cast<float>((static_cast<double>(max) - m_points) * Tenth() * Half())) +
        m_points);
    if (!(m_points <= max))
        m_points = max;
    if (m_points < Zero())
        m_points = 0.0f;

    // The judges: two near the points, the third making the average.
    float* judges = m_judgeScores;
    for (int i = 0; i < 2; ++i) {
        judges[i] = static_cast<float>(Mth::PlusOrMinusExact(spread) + m_points);
        if (!(judges[i] <= MaxJudge()))
            judges[i] = max;
        if (judges[i] < Zero())
            judges[i] = 0.0f;
    }
    judges[2] = static_cast<float>(static_cast<double>(m_points) * Three() -
                                   (static_cast<double>(judges[1]) + judges[0]));
    if (judges[2] < Zero())
        judges[2] = 0.0f;
    if (!(judges[2] <= MaxJudge()))
        judges[2] = max;
    // Each to a tenth (the third with a nudge up), then to a whole score.
    judges[2] = static_cast<float>(Truncate(static_cast<double>(judges[2]) * Ten()) * static_cast<double>(Tenth()) +
                                   Tiny());
    judges[0] = static_cast<float>(
        Truncate(Truncate(static_cast<double>(judges[0]) * Ten()) * static_cast<double>(Tenth()) + OverHalf()));
    float first = judges[0];
    double second = Truncate(Truncate(static_cast<double>(judges[1]) * Ten()) * static_cast<double>(Tenth()) +
                             OverHalf());
    judges[1] = static_cast<float>(second);
    judges[2] = static_cast<float>(Truncate(static_cast<double>(judges[2]) + NearlyHalf()));
    float limit = NinetyNine();
    if (!(first <= limit))
        judges[0] = limit;
    if (!(second <= limit))
        judges[1] = limit;
    if (!(judges[2] <= limit))
        judges[2] = limit;
    m_points = static_cast<float>(Truncate(
        (static_cast<double>(judges[1]) + judges[2] + judges[0]) * Third() + NearlyHalf()));
    if (!(m_points <= limit))
        m_points = limit;
    m_runScores[m_runs] = Truncate(m_points);
    Dbg::Printf(reinterpret_cast<const char*>(0x0064D92C), static_cast<double>(judges[0]),
                static_cast<double>(judges[1]), static_cast<double>(judges[2]), static_cast<double>(m_points));

    // The player's score of the run, the standings, the place.
    for (Leader& leader : m_leaders) {
        if (leader.m_id == 0)
            leader.m_scores[m_runs] = m_runScores[m_runs];
    }
    SortLeaders();
    for (int i = 0; i < kLeaders; ++i) {
        if (m_leaders[i].m_id == 0)
            m_place = i + 1;
    }

    // The two other judges near the lowest of the three.
    float lowest = judges[0];
    if (judges[1] < lowest)
        lowest = judges[1];
    if (judges[2] < lowest)
        lowest = judges[2];
    Dbg::Printf(reinterpret_cast<const char*>(0x0064D91C), static_cast<double>(lowest));
    float base = lowest - spread;
    for (int i = 3; i < kJudges; ++i) {
        double judge = Mth::PlusOrMinusExact(spread) + base;
        judges[i] = static_cast<float>(judge);
        Dbg::Printf(reinterpret_cast<const char*>(0x0064D90C), i, judge); // (the unrounded value)
    }
    for (int i = 0; i < kJudges; ++i) {
        if (judges[i] < Zero())
            judges[i] = 0.0f;
    }
    // The first four shuffled.
    for (int i = 0; i < kJudges - 1; ++i) {
        int other = Mth::Rnd2(kJudges);
        float swap = judges[i];
        judges[i] = judges[other];
        judges[other] = swap;
    }

    if (!CompetitionGoalActive())
        return;
    Script::CStruct* params = NewStructure();
    Script::CArray* scores = NewArray();
    scores->SetSizeAndType(5, Script::ESYMBOLTYPE_STRUCTURE);
    Dbg::Printf(reinterpret_cast<const char*>(0x0064D8F8));
    for (int i = 0; i < kJudges; ++i) {
        Script::CStruct* entry = NewStructure();
        entry->AddInteger(reinterpret_cast<const char*>(0x0064D384), Round(judges[i])); // "score"
        if (IsTopJudge(i))
            entry->AddChecksum(kNoName, kTopJudge);
        scores->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(entry));
        Dbg::Printf(reinterpret_cast<const char*>(0x0064D8E8), i, static_cast<double>(judges[i]),
                    static_cast<int>(IsTopJudge(i)));
    }
    params->AddArray(reinterpret_cast<const char*>(0x0064D8E0), scores);              // "scores"
    params->AddInteger(reinterpret_cast<const char*>(0x0064D8D4), Round(m_points));  // "total_score"
    FreeArray(scores);
    Script::SpawnScript(reinterpret_cast<const char*>(0x0064D8B8), params, 0, nullptr, 0, 0, false, false, false,
                        false); // goal_comp_show_run_scores
    DeleteStructure(params);
}

} // namespace Mdl

REPLACE(0x0054FFD0, &Mdl::CCompetition::Leader::Construct);
REPLACE(0x0054FFF0, &Mdl::CCompetition::DestroyBody);
REPLACE(0x00550010, &Mdl::CCompetition::EditParams);
REPLACE(0x00550020, &Mdl::CCompetition::StartRun);
REPLACE(0x00550040, &Mdl::CCompetition::IsTopJudge);
REPLACE(0x005501A0, &Mdl::CCompetition::PlaceIs);
REPLACE(0x005501C0, &Mdl::CCompetition::RoundIs);
REPLACE(0x005501E0, &Mdl::CCompetition::End);
REPLACE(0x005501F0, &Mdl::CCompetition::IsEnded);
REPLACE(0x00550200, &Mdl::CCompetition::SortLeaders);
REPLACE(0x005504B0, &Mdl::CCompetition::Construct);
REPLACE(0x00550560, &Mdl::CCompetition::Start);
REPLACE(0x00550B50, &Mdl::CCompetition::ShowLeaderBoard);
REPLACE(0x00550EF0, &Mdl::CCompetition::EndRun);
