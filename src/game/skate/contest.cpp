// GameModules::CContest: 0x0054FFD0-0x005517BF (see contest.h).

#include "game/skate/contest.h"

#include <cstdint>
#include <new>

#include "game/core/checksums.h"
#include "game/core/dbg.h"
#include "game/maths/random.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/skate/profile_manager.h"
#include "game/goals/goal_manager.h"
#include "game/script/token_reader.h"
#include "game/obj/anim_movies.h"
#include "game/gfx/tod_manager.h"
#include "game/gfx/loading_bar.h"

namespace GameModules {

float g_competitionTiny{0.0001f};
float g_competitionTwo{2.0f};
float g_competitionThird{0.33333334f};
float g_competitionThree{3.0f};
float g_competitionEighth{0.125f};
float g_competitionNearlyHalf{0.499999f};
float g_competitionNinetyNine{99.0f};
float g_competitionOverHalf{0.500099f};
float g_competitionMaxJudge{99.85f};
float g_competitionMaxPoints{99.9f};

namespace {

constexpr uint32_t kNoName = 0;                    // (an unnamed component: a flag)
constexpr uint32_t kLeaderBoardNames = 0x1CC6F48D;  // leader_board_names
constexpr uint32_t kFirstPlaceName = 0xFAF19012;    // first_place_name
constexpr uint32_t kName = 0xA1DC81F9;              // Name
constexpr uint32_t kScore = 0xCD66C8AE;             // score
constexpr uint32_t kPlayer = 0x67E6859A;            // player
constexpr uint32_t kTopJudge = 0x7B1B959F;          // top_judge
constexpr uint32_t kRankingList = 0x370B9420;       // leader_board
constexpr uint32_t kAddLeaderBoard = 0xCE76F8C1;    // goal_comp_add_leader_board
constexpr uint32_t kJudgeNames[CContest::kJudges] = {
    0x84F93830, 0x1DF0698A, 0x6AF7591C, 0xF493CCBF, 0x8394FC29, // comp_str_Judge1..5
};

// The constants, as the original reads them.
float Tenth() { return Scripts::g_parseNudge; }        // 0.1
float Eighth() { return g_competitionEighth; }       // 0.125
float Two() { return g_competitionTwo; }          // 2.0
float NearlyHalf() { return g_competitionNearlyHalf; }   // 0.4999990 (rounding to the nearest)
float Hundred() { return Gfx2D::g_hundred; }      // 100
float MaxPoints() { return g_competitionMaxPoints; }    // 99.9
float Half() { return Gfx2D::g_half; }         // 0.5
float Zero() { return GameObjects::g_floatZero; }         // 0
float MaxJudge() { return g_competitionMaxJudge; }     // 99.85
float Three() { return g_competitionThree; }        // 3
float Ten() { return Engine3D::g_floatTen; }          // 10
float Tiny() { return g_competitionTiny; }         // 0.0001
float OverHalf() { return g_competitionOverHalf; }     // 0.500099
float NinetyNine() { return g_competitionNinetyNine; }   // 99
float Third() { return g_competitionThird; }        // 1/3
constexpr float kScoreSpread = 1.9f;                             // (0x3FF33333, pushed as an immediate)

// The x87 code's conversions: truncation toward zero (_ftol).
int Truncate(double value) { return static_cast<int>(value); }
// A score rounded to the nearest (the original adds 0.499999 and truncates).
int Round(float value) { return Truncate(static_cast<double>(value) + NearlyHalf()); }

GameObjects::CProfileRoster* RosterHandle() { return GameObjects::g_profileRoster; }
// The profile manager's template at `index` (0x00557FB0) and its count (0x00469490: the table's).
GameObjects::CRiderIdentity* TemplateAt(GameObjects::CProfileRoster* manager, int index)
{
    // (0x00557FB0 is the lookup table's by-index code, shared with the goal manager's: its first member is the table too)
    return reinterpret_cast<GameObjects::CRiderIdentity*>(reinterpret_cast<GoalSystem::CGoalManager*>(manager)->GetObjectiveAt(index));
}

// The goal manager (0x005591B0) and its active competition goal (0x00558D90; null: none).
bool CompetitionGoalActive()
{
    return GoalSystem::GetObjectiveBoard()->GetActiveCompetition() != nullptr;
}

Scripts::CStruct* SpawnParamTable()
{
    Scripts::CStruct* structure = Scripts::SpawnParamBag();
    return structure ? structure->PrepareAll() : nullptr;
}
void DeleteStructure(Scripts::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}
Scripts::CArray* AllocScriptArray()
{
    Scripts::CArray* array = Scripts::AllocScriptArray();
    return array ? array->PrepareAll() : nullptr;
}
// An array handed to a structure (which copied it): its contents and itself freed.
void FreeArray(Scripts::CArray* array)
{
    Scripts::ReleaseArrayStorage(array);
    if (array) {
        array->Clear();
        Scripts::DeleteArray(array);
    }
}

} // namespace

int CContest::Leader::BestTwo(int count) const
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

int CContest::Leader::BestTwoOfFourWords() const
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
CContest::Leader* CContest::Leader::MakeNew()
{
    new (&m_label) TextUtil::Chars();
    return this;
}

// 0x0054FFF0
void CContest::DestroyBody()
{
    // (the compiler's array-destructor helper, 0x0062382B: last to first)
    for (int i = 7; i >= 0; --i)
        m_leaders[i].m_label.~Chars();
}

// 0x00550010
void CContest::RewriteArgs(Scripts::CStruct* params) { m_args->AppendStructureIfAny(params); }

// 0x00550020
void CContest::BeginTimedSession()
{
    ++m_runs;
    m_pathVertices = 0.0f;
    m_crashPenalty = 0.0f;
}

// 0x00550040
bool CContest::IsLeadingScorer(int judge)
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
bool CContest::PlaceIs(int place) { return place == m_standing; }

// 0x005501C0
bool CContest::RoundIs(int round) { return round == m_runs + 1; }

// 0x005501E0
void CContest::End() { m_ended = true; }

// 0x005501F0
bool CContest::IsEnded() { return m_ended; }

// 0x00550200
void CContest::SortLeaders()
{
    int count = (m_ended ? 2 : m_runs) + 1;
    for (int i = 0; i < kLeaders - 1; ++i) {
        int best = i;
        for (int j = i + 1; j < kLeaders; ++j) {
            int score = m_leaders[j].BestTwo(count);
            int bestScore = m_leaders[best].BestTwo(count);
            if (score > bestScore || (m_leaders[j].m_ident == 0 && score == bestScore))
                best = j;
        }
        if (best != i) {
            // (a leader's copy: the name, the id, the scores and m_unknown1C - not the padding)
            Leader& a = m_leaders[best];
            Leader& b = m_leaders[i];
            TextUtil::Chars name(a.m_label);
            int id = a.m_ident;
            int scores[kRuns] = {a.m_scores[0], a.m_scores[1], a.m_scores[2]};
            bool unknown = a.m_unknown1C;
            a.m_label = b.m_label;
            a.m_ident = b.m_ident;
            for (int k = 0; k < kRuns; ++k)
                a.m_scores[k] = b.m_scores[k];
            a.m_unknown1C = b.m_unknown1C;
            b.m_label = name;
            b.m_ident = id;
            for (int k = 0; k < kRuns; ++k)
                b.m_scores[k] = scores[k];
            b.m_unknown1C = unknown;
        }
    }
}

// 0x005504B0
CContest* CContest::MakeNew(int)
{
    // (the compiler's array-constructor helper, 0x0062388B)
    for (Leader& leader : m_leaders)
        leader.MakeNew();
    m_args = SpawnParamTable();
    return this;
}

// 0x00550560
void CContest::Kickoff(float bronze, float silver, float gold, float bronzeScore, float silverScore,
                         float goldScore, float bailPenalty)
{
    m_thirdMark = bronze;
    m_secondMark = silver;
    m_topMark = gold;
    m_bronzeScore = bronzeScore;
    m_silverScore = silverScore;
    m_goldScore = goldScore;
    m_bailPenalty = bailPenalty;
    m_ended = false;

    GameObjects::CProfileRoster* manager = RosterHandle();
    m_leaders[0].m_label = manager->FetchActiveAccount()->FetchShownName();

    // The pros that can compete: not hidden (0xF3149AC5) and not the player, in a random order.
    int pros[96]; // (the original's stack room)
    int proCount = 0;
    int templates = static_cast<int>(manager->m_templates.m_num);
    for (int i = 0; i < templates; ++i) {
        GameObjects::CRiderIdentity* profile = TemplateAt(manager, i);
        if (profile->IsPro() && !profile->HasFlagF3149AC5() &&
            Checksums::HashFromText(profile->FetchShownName()) !=
                Checksums::HashFromText(m_leaders[0].m_label.c_str()))
            pros[proCount++] = i;
    }
    for (int i = 0; i < proCount; ++i) {
        int other = Maths::RollSideStream(proCount);
        int swap = pros[i];
        pros[i] = pros[other];
        pros[other] = swap;
    }
    int ids[kLeaders - 1] = {1, 2, 3, 4, 5, 6, 7};
    for (int i = 0; i < kLeaders - 1; ++i) {
        int other = Maths::RollSideStream(kLeaders - 1);
        int swap = ids[i];
        ids[i] = ids[other];
        ids[other] = swap;
    }

    m_leaders[0].m_ident = 0;
    for (int& score : m_leaders[0].m_scores)
        score = 0;
    bronze = static_cast<float>(Maths::PlusOrMinusExact(Two()) + bronze);
    silver = static_cast<float>(Maths::PlusOrMinusExact(Two()) + silver);
    gold = static_cast<float>(Maths::PlusOrMinusExact(Two()) + gold);
    if (!(silver < gold))
        silver = static_cast<float>(static_cast<double>(gold) - Tenth());
    if (!(bronze < silver))
        bronze = static_cast<float>(static_cast<double>(silver) - Tenth());

    for (int i = 0; i < kLeaders - 1; ++i) {
        Leader& leader = m_leaders[i + 1];
        leader.m_ident = ids[i];
        Scripts::CArray* names;
        const char* name;
        if (m_args->FetchList(kLeaderBoardNames, &names, 0))
            name = names->FindInlineText(static_cast<uint32_t>(i));
        else
            name = manager->GetTemplate(manager->GetTemplateName(pros[i]))->FetchShownName();
        leader.m_label = name;

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
            double random = Maths::PlusOrMinusExact(spread);
            points = static_cast<float>(static_cast<double>(bronze) - spread + random);
            if (!(points < bronze))
                points = static_cast<float>(static_cast<double>(bronze) - Tenth());
        }
        // Two judges' scores either side of the points, the third a little lower.
        float high = static_cast<float>(Maths::PlusOrMinusExact(Two()) + points);
        float low = high;
        float mirrored = static_cast<float>(static_cast<double>(points) + points - high);
        if (mirrored < high)
            low = mirrored;
        int first = Maths::RollSideStream(3);
        int second;
        do {
            second = Maths::RollSideStream(3);
        } while (second == first);
        float base = low - Two();
        for (int& score : leader.m_scores)
            score = Truncate(Maths::PlusOrMinusExact(kScoreSpread) + base + NearlyHalf());
        leader.m_scores[first] = Round(high);
        leader.m_scores[second] = Round(mirrored);
    }

    DebugOut::Printf("\n\n");
    for (int i = 0; i < kLeaders; ++i) {
        Leader& leader = m_leaders[i];
        DebugOut::Printf("%d: pos %d, name %12s, %3d, %3d, %3d, %3d\n", i, leader.m_ident, leader.m_label.c_str(), leader.Word(0),
                    leader.Word(1), leader.Word(2), leader.BestTwoOfFourWords());
    }
    SortLeaders();
    const char* firstPlace;
    if (m_args->FetchText(kFirstPlaceName, &firstPlace, 0))
        m_leaders[0].m_label = firstPlace;
    DebugOut::Printf("\nSorted\n");
    for (int i = 0; i < kLeaders; ++i) {
        Leader& leader = m_leaders[i];
        DebugOut::Printf("%d: pos %d, name %12s, %3d, %3d, %3d, %3d\n", i, leader.m_ident, leader.m_label.c_str(), leader.Word(0),
                    leader.Word(1), leader.Word(2), leader.BestTwoOfFourWords());
    }
    for (int i = 0; i < kJudges; ++i)
        m_judgeNames[i] = Scripts::FetchText(kJudgeNames[i]);
    m_runs = -1;
}

// 0x00550B50
void CContest::ShowLeaderBoard()
{
    if (!CompetitionGoalActive())
        return;
    Scripts::CStruct* params = SpawnParamTable();
    Scripts::CArray* board = AllocScriptArray();
    board->ConfigureSizeKind(5, Scripts::QBVAL_STRUCT);
    DebugOut::Printf("\nSetupJudgeText\n");
    for (int i = 0; i < kLeaders; ++i) {
        Leader& leader = m_leaders[i];
        DebugOut::Printf("%d: pos %d, name %12s, %3d, %3d, %3d, %3d\n", i, leader.m_ident, leader.m_label.c_str(), leader.Word(0),
                    leader.Word(1), leader.Word(2), leader.BestTwoOfFourWords());
    }
    for (int i = 0; i < 5; ++i) {
        Leader& leader = m_leaders[i];
        Scripts::CStruct* entry = SpawnParamTable();
        entry->AppendWord(kName, leader.m_label.c_str());
        DebugOut::Printf("m_current_round: %i\n", m_runs);
        entry->PutInt(kScore, leader.BestTwo(m_runs + 1));
        if (leader.m_ident == 0)
            entry->PutNameKey(kNoName, kPlayer);
        board->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(entry));
    }
    params->AppendArrayValue(kRankingList, board);
    FreeArray(board);
    Scripts::SpawnScript(kAddLeaderBoard, params, 0, nullptr, 0, 0, false, false, false, false);
    DeleteStructure(params);
}

// 0x00550EF0
void CContest::EndRun(int score)
{
    if (m_ended)
        return;
    DebugOut::Printf("Ending Competition with score of %d\n", score);
    DebugOut::Printf("Bronze %f, silver %f, gold %f\n", static_cast<double>(m_bronzeScore),
                static_cast<double>(m_silverScore), static_cast<double>(m_goldScore));

    // The points: linear between the medal scores (above gold: towards 100 at twice the gold score).
    double x = score;
    float points;
    if (x < m_bronzeScore)
        points = static_cast<float>(static_cast<double>(m_thirdMark) / m_bronzeScore * x);
    else if (x < m_silverScore)
        points = static_cast<float>((static_cast<double>(m_secondMark) - m_thirdMark) * (x - m_bronzeScore) /
                                        (static_cast<double>(m_silverScore) - m_bronzeScore) +
                                    m_thirdMark);
    else if (x < m_goldScore)
        points = static_cast<float>((static_cast<double>(m_topMark) - m_secondMark) * (x - m_silverScore) /
                                        (static_cast<double>(m_goldScore) - m_silverScore) +
                                    m_secondMark);
    else
        points = static_cast<float>((x - m_goldScore) * (static_cast<double>(Hundred()) - m_topMark) /
                                        (static_cast<double>(m_goldScore) + m_goldScore - m_goldScore) +
                                    m_topMark);
    DebugOut::Printf("points calculated at %f\n", static_cast<double>(points));
    points = points - m_crashPenalty;
    DebugOut::Printf("points decremented by %f bail points to %f\n", static_cast<double>(m_crashPenalty),
                static_cast<double>(points));

    float max = MaxPoints();
    m_pathVertices = points;
    if (!(points <= max))
        m_pathVertices = max;
    float spread = static_cast<float>((static_cast<double>(max) - m_pathVertices) * Tenth());
    m_pathVertices = static_cast<float>(
        Maths::PlusOrMinusExact(static_cast<float>((static_cast<double>(max) - m_pathVertices) * Tenth() * Half())) +
        m_pathVertices);
    if (!(m_pathVertices <= max))
        m_pathVertices = max;
    if (m_pathVertices < Zero())
        m_pathVertices = 0.0f;

    // The judges: two near the points, the third making the average.
    float* judges = m_judgeScores;
    for (int i = 0; i < 2; ++i) {
        judges[i] = static_cast<float>(Maths::PlusOrMinusExact(spread) + m_pathVertices);
        if (!(judges[i] <= MaxJudge()))
            judges[i] = max;
        if (judges[i] < Zero())
            judges[i] = 0.0f;
    }
    judges[2] = static_cast<float>(static_cast<double>(m_pathVertices) * Three() -
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
    m_pathVertices = static_cast<float>(Truncate(
        (static_cast<double>(judges[1]) + judges[2] + judges[0]) * Third() + NearlyHalf()));
    if (!(m_pathVertices <= limit))
        m_pathVertices = limit;
    m_runTotals[m_runs] = Truncate(m_pathVertices);
    DebugOut::Printf("3 scores are %f,%f,%f, total is %f\n", static_cast<double>(judges[0]),
                static_cast<double>(judges[1]), static_cast<double>(judges[2]), static_cast<double>(m_pathVertices));

    // The player's score of the run, the standings, the place.
    for (Leader& leader : m_leaders) {
        if (leader.m_ident == 0)
            leader.m_scores[m_runs] = m_runTotals[m_runs];
    }
    SortLeaders();
    for (int i = 0; i < kLeaders; ++i) {
        if (m_leaders[i].m_ident == 0)
            m_standing = i + 1;
    }

    // The two other judges near the lowest of the three.
    float lowest = judges[0];
    if (judges[1] < lowest)
        lowest = judges[1];
    if (judges[2] < lowest)
        lowest = judges[2];
    DebugOut::Printf("Lowest = %f\n", static_cast<double>(lowest));
    float base = lowest - spread;
    for (int i = 3; i < kJudges; ++i) {
        double judge = Maths::PlusOrMinusExact(spread) + base;
        judges[i] = static_cast<float>(judge);
        DebugOut::Printf("Score %d is %f\n", i, judge); // (the unrounded value)
    }
    for (int i = 0; i < kJudges; ++i) {
        if (judges[i] < Zero())
            judges[i] = 0.0f;
    }
    // The first four shuffled.
    for (int i = 0; i < kJudges - 1; ++i) {
        int other = Maths::RollSideStream(kJudges);
        float swap = judges[i];
        judges[i] = judges[other];
        judges[other] = swap;
    }

    if (!CompetitionGoalActive())
        return;
    Scripts::CStruct* params = SpawnParamTable();
    Scripts::CArray* scores = AllocScriptArray();
    scores->ConfigureSizeKind(5, Scripts::QBVAL_STRUCT);
    DebugOut::Printf("\nJudgement Day\n\n");
    for (int i = 0; i < kJudges; ++i) {
        Scripts::CStruct* entry = SpawnParamTable();
        entry->PutInt("score", Round(judges[i])); // "score"
        if (IsLeadingScorer(i))
            entry->PutNameKey(kNoName, kTopJudge);
        scores->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(entry));
        DebugOut::Printf("%d: %f Top=%d\n", i, static_cast<double>(judges[i]),
                    static_cast<int>(IsLeadingScorer(i)));
    }
    params->AppendArrayValue("scores", scores);              // "scores"
    params->PutInt("total_score", Round(m_pathVertices));  // "total_score"
    FreeArray(scores);
    Scripts::SpawnScript("goal_comp_show_run_scores", params, 0, nullptr, 0, 0, false, false, false,
                        false); // goal_comp_show_run_scores
    DeleteStructure(params);
}

} // namespace Mdl
