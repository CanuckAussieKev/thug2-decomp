// Script commands of the game modes' runs: split screen options, competitions, horse, score options, bonuses
// (see competition_cfuncs.h).

#include "game/skate/competition_cfuncs.h"

#include <cstdint>
#include <new>

#include "game/core/manager.h"
#include "game/core/string.h"
#include "game/obj/object.h"
#include "game/obj/trick_objects.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/skate/competition.h"
#include "game/skate/skate_module_calls.h"
#include "game/skater/skater.h"
#include "game/skate/horse.h"
#include "game/skate/score.h"
#include "game/skate/game_mode.h"
#include "game/sys/game_config.h"

namespace ScriptCommands {

namespace {

using namespace SkateCalls;

constexpr uint32_t kGameType = 0x9834C4FA;        // game_type
constexpr uint32_t kChecksum = 0x21902065;        // checksum
constexpr uint32_t kTimeLimit = 0xC51BF6E3;       // time_limit
constexpr uint32_t kTime = 0x906B67BA;            // time
constexpr uint32_t kIsHorse = 0xD1993397;         // is_horse
constexpr uint32_t kHorseTimeLimit = 0xC146142E;  // horse_time_limit
constexpr uint32_t kHorseWord = 0x6EA5ADDD;       // horse_word
constexpr uint32_t kUiString = 0x96875C0F;        // ui_string
constexpr uint32_t kDefaultTimeLimit = 0x8D38A280; // default_time_limit
constexpr uint32_t kExtraParams = 0xA61DC9A4;     // extra_params
constexpr uint32_t kWhichString = 0xB8D0CCC8;     // whichString
constexpr uint32_t kId = 0x40C698AF;              // id
constexpr uint32_t kText = 0xC4745838;            // text
constexpr uint32_t kCreateHorsePanelMessage = 0x621637CE; // create_horse_panel_message
constexpr uint32_t kFreezeScore = 0x294C84E1;     // freeze_score
constexpr uint32_t kSkater = 0x5B8AB877;          // skater
constexpr uint32_t kBonus = 0x60678085;           // bonus

// The skate module's split screen preferences (0x00564D80: +0x58) and its time limit (0x00564D50: +0x370).
OptionStore::Preferences* SplitScreenPreferences()
{
    return GameModules::Skate::Sole()->GetSplitViewSettings();
}
void SetCountdownSeconds(int seconds) { GameModules::Skate::Sole()->SetUnknown370(seconds); }

// The game mode: 0x00553AD0 the mode `type` loaded (its structure from the mode list); 0x00553BE0 its options
// overridden by `options`; 0x00553000 / 0x00553020 / 0x00553040 / 0x00553060 its accumulate_score / score_frozen /
// degrade_score / track_trick_score.
void LoadGameMode(uint32_t type) { static_cast<GoalSystem::CMatchRules*>(GameMode())->ApplyMatchStyle(type); }
void OverrideGameModeOptions(Scripts::CStruct* options)
{
    static_cast<GoalSystem::CMatchRules*>(GameMode())->ForceMatchSettings(options);
}
void SetGameModeFlag(void (GoalSystem::CMatchRules::*setter)(bool), bool on) { (static_cast<GoalSystem::CMatchRules*>(GameMode())->*setter)(on); }
constexpr auto kSetAccumulateScore = &GoalSystem::CMatchRules::SetScoreAccumulation;
constexpr auto kSetScoreFrozen = &GoalSystem::CMatchRules::HoldPoints;
constexpr auto kSetDegradeScore = &GoalSystem::CMatchRules::SetScoreDegradation;
constexpr auto kSetTrackTrickScore = &GoalSystem::CMatchRules::SetTrickScoreTracking;

// The competition (GameModules::CContest, no source for these yet): 0x00550560 started with the medal scores and the
// bail penalty; 0x00550EF0 a run ended with the local skater's score; 0x00550B50 the standings sorted; 0x005501A0
// the place is `place`; 0x005501C0 the round is `round` (the runs + 1); 0x005501E0 ended; 0x005501F0 whether ended.
GameModules::CContest* TheCompetition() { return reinterpret_cast<GameModules::CContest*>(Competition()); }

// The horse game (no source yet): 0x00560100 started; 0x00560180 a run started; 0x00560720 a run ended; 0x005608B0
// the next player; 0x00560380 whether it's over; 0x00560190 its status is the one `params` names; 0x00560440 one of
// its strings (`which`); 0x005601F0 its word (H-O-R-S-E) set. The current skater's id is its +0x04 (0x005480C0).
GameModules::CLetterMatch* TheHorse() { return reinterpret_cast<GameModules::CLetterMatch*>(Horse()); }
uint32_t CurrentHorseSkater() { return *reinterpret_cast<uint32_t*>(Horse() + 4); }
// The horse's string `which`, built in `storage` (the caller destroys it).
TextUtil::Chars* GetHorseString(void* storage, uint32_t which)
{
    return new (storage) TextUtil::Chars(TheHorse()->FetchText(which));
}

// A skater's score (CRider::GetPointsTotal): 0x00561B40 its total; 0x00561410 / 0x00561420 its bonus (+0x28) added to /
// read.
uint8_t* ScoreOf(GameObjects::CGameEntity* skater)
{
    return static_cast<uint8_t*>(static_cast<GameObjects::CRider*>(skater)->GetPointsTotal());
}
int TotalScore(uint8_t* score) { return reinterpret_cast<GameModules::PointTally*>(score)->GetTotalScore(); }
void AddToBonus(uint8_t* score, int bonus) { reinterpret_cast<GameModules::PointTally*>(score)->AddBonus(bonus); }
int GetBonus(uint8_t* score) { return reinterpret_cast<GameModules::PointTally*>(score)->GetBonus(); }

// The skater info heap, current while the competition's standings change.
void PushSkaterInfoHeap()
{
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_skaterInfoHeap);
}
void PopHeap() { MemSys::Hub::Pull()->LeaveHeapScope(); }

Scripts::CStruct* NewParams()
{
    Scripts::CStruct* memory = Scripts::NewStruct();
    return memory ? memory->PrepareAll() : nullptr;
}
void DeleteParams(Scripts::CStruct* params)
{
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
}

} // namespace

// 0x005AF090: ApplySplitScreenOptions - the split screen preferences' game type loaded into the game mode; for horse
// its time limit and word applied too (the game mode's default_time_limit and the skate module's time limit). (The
// time_limit preference is read and not used.)
bool __cdecl CFunc_ApplySplitScreenOptions(Scripts::CStruct*, Scripts::CScript*)
{
    OptionStore::Preferences* prefs = SplitScreenPreferences();
    uint32_t type = 0;
    prefs->FetchSetting(kGameType)->ReadHashValue(kChecksum, &type, 1);
    LoadGameMode(type);
    prefs->GetSettingValue(kTimeLimit, kTime);
    if (GameModeIsTrue(kIsHorse)) {
        int timeLimit = prefs->GetSettingValue(kHorseTimeLimit, kTime);
        const char* word = prefs->GetPreferenceString(kHorseWord, kUiString);
        // (the string is passed by value: the horse's code destroys it)
        TheHorse()->SetLetters(TextUtil::Chars(word));
        Scripts::CStruct* options = NewParams();
        options->AddComponentOfType(kDefaultTimeLimit, Scripts::QBVAL_INTEGER, static_cast<uint32_t>(timeLimit));
        OverrideGameModeOptions(options);
        SetCountdownSeconds(timeLimit);
        DeleteParams(options);
    }
    return true;
}

// 0x005AF200: StartCompetition gold= silver= bronze= gold_score= silver_score= bronze_score= bail= [extra_params=]
// - a competition started (in the skater info heap), the extra parameters added to the competition's first.
bool __cdecl CFunc_StartCompetition(Scripts::CStruct* params, Scripts::CScript*)
{
    // (uninitialised in the original when missing)
    float gold = 0.0f, silver = 0.0f, bronze = 0.0f, goldScore = 0.0f, silverScore = 0.0f, bronzeScore = 0.0f,
          bail = 0.0f;
    params->FetchFloat("gold", &gold, 0);
    params->FetchFloat("silver", &silver, 0);
    params->FetchFloat("bronze", &bronze, 0);
    params->FetchFloat("gold_score", &goldScore, 0);
    params->FetchFloat("silver_score", &silverScore, 0);
    params->FetchFloat("bronze_score", &bronzeScore, 0);
    params->FetchFloat("bail", &bail, 0);
    Scripts::CStruct* extra = nullptr;
    params->ReadSubStruct(kExtraParams, &extra, 0);
    PushSkaterInfoHeap();
    if (extra)
        TheCompetition()->RewriteArgs(extra);
    TheCompetition()->Kickoff(bronze, silver, gold, bronzeScore, silverScore, goldScore, bail);
    PopHeap();
    return true;
}

// 0x005AF320: StartCompetitionRun.
bool __cdecl CFunc_StartCompetitionRun(Scripts::CStruct*, Scripts::CScript*)
{
    TheCompetition()->BeginTimedSession();
    return true;
}

// 0x005AF340: EndCompetitionRun - the local skater's total score ends the run; the standings sorted (in the skater
// info heap).
bool __cdecl CFunc_EndCompetitionRun(Scripts::CStruct*, Scripts::CScript*)
{
    int score = TotalScore(ScoreOf(GetHomeRider()));
    PushSkaterInfoHeap();
    TheCompetition()->EndRun(score);
    TheCompetition()->ShowLeaderBoard();
    PopHeap();
    return true;
}

// 0x005AF3A0 / 0x005AF3D0: PlaceIs <n> / RoundIs <n>.
bool __cdecl CFunc_PlaceIs(Scripts::CStruct* params, Scripts::CScript*)
{
    int place = 0; // (uninitialised in the original)
    params->FetchIntValue(0u, &place, 0);
    return TheCompetition()->PlaceIs(place);
}

bool __cdecl CFunc_RoundIs(Scripts::CStruct* params, Scripts::CScript*)
{
    int round = 0; // (uninitialised in the original)
    params->FetchIntValue(0u, &round, 0);
    return TheCompetition()->RoundIs(round);
}

// 0x005AF400: EndCompetition (in the skater info heap). 0x005AF440: CompetitionEnded.
bool __cdecl CFunc_EndCompetition(Scripts::CStruct*, Scripts::CScript*)
{
    PushSkaterInfoHeap();
    TheCompetition()->End();
    PopHeap();
    return true;
}

bool __cdecl CFunc_CompetitionEnded(Scripts::CStruct*, Scripts::CScript*)
{
    return TheCompetition()->IsEnded();
}

// 0x005AF450 StartHorse, 0x005AF470 HorseEnded, 0x005AF480 HorseStatusEquals <status>, 0x005AF4A0 StartHorseRun,
// 0x005AF4C0 EndHorseRun, 0x005AF4E0 SwitchHorsePlayers.
bool __cdecl CFunc_StartHorse(Scripts::CStruct*, Scripts::CScript*)
{
    TheHorse()->PrepareAll();
    return true;
}

bool __cdecl CFunc_HorseEnded(Scripts::CStruct*, Scripts::CScript*)
{
    return TheHorse()->IsOver();
}

bool __cdecl CFunc_HorseStatusEquals(Scripts::CStruct* params, Scripts::CScript*)
{
    return TheHorse()->StateMatches(params);
}

bool __cdecl CFunc_StartHorseRun(Scripts::CStruct*, Scripts::CScript*)
{
    TheHorse()->BeginTimedSession();
    return true;
}

bool __cdecl CFunc_EndHorseRun(Scripts::CStruct*, Scripts::CScript*)
{
    TheHorse()->EndRun();
    return true;
}

bool __cdecl CFunc_SwitchHorsePlayers(Scripts::CStruct*, Scripts::CScript*)
{
    TheHorse()->SwapRiders();
    return true;
}

// 0x005AF500: ApplyToHorsePanelString whichString=<s> id=<id> ... - create_horse_panel_message run on the current
// horse skater with the parameters, the string as text and the id.
bool __cdecl CFunc_ApplyToHorsePanelString(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t which = 0, id = 0;
    params->ReadHashValue(kWhichString, &which, 1);
    params->ReadHashValue(kId, &id, 1);
    alignas(4) uint8_t storage[sizeof(TextUtil::Chars)];
    TextUtil::Chars* text = GetHorseString(storage, which);
    GameObjects::CGameEntity* skater = GetRiderByNumber(CurrentHorseSkater());
    Scripts::CStruct* panel = NewParams();
    panel->MergeStructure(params);
    panel->AppendWord(kText, text->c_str());
    panel->PutNameKey(kId, id);
    Scripts::LaunchScript(kCreateHorsePanelMessage, panel, skater, false, nullptr);
    DeleteParams(panel);
    text->~Chars();
    return true;
}

// 0x005AF660: GetHorseString whichString=<s> - the horse's string as text.
bool __cdecl CFunc_GetHorseString(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t which = 0;
    params->ReadHashValue(kWhichString, &which, 1);
    alignas(4) uint8_t storage[sizeof(TextUtil::Chars)];
    TextUtil::Chars* text = GetHorseString(storage, which);
    script->m_args->AppendWord(kText, text->c_str());
    text->~Chars();
    return true;
}

// 0x005AF700: IsCurrentHorseSkater <id> - whether the current horse skater's id is the integer (or checksum) given.
bool __cdecl CFunc_IsCurrentHorseSkater(Scripts::CStruct* params, Scripts::CScript*)
{
    int id = 0; // (uninitialised in the original)
    if (!params->FetchIntValue(0u, &id, 0))
        params->ReadHashValue(0u, reinterpret_cast<uint32_t*>(&id), 1);
    return CurrentHorseSkater() == static_cast<uint32_t>(id);
}

// 0x005AFB40: TrickOffAllObjects <player> - the trick objects nobody holds given to the player (not with the debug
// checks flag 0x00684FA8 set).
bool __cdecl CFunc_TrickOffAllObjects(Scripts::CStruct* params, Scripts::CScript*)
{
    if (!GameConfig::g_fromCD) {
        int player = 0;
        params->FetchIntValue(0u, &player, 0);
        GameObjects::g_trickObjectManager->GiveFreeClusters(player);
    }
    return true;
}

// 0x005AFB80 / 0x005AFBC0: SetScoreDegradation <0/1> / SetTrickScoreTracking <0/1> (default 1): the game mode's
// degrade_score / track_trick_score.
bool __cdecl CFunc_SetScoreDegradation(Scripts::CStruct* params, Scripts::CScript*)
{
    int on = 1;
    params->FetchIntValue(0u, &on, 0);
    SetGameModeFlag(kSetDegradeScore, on != 0);
    return true;
}

bool __cdecl CFunc_SetTrickScoreTracking(Scripts::CStruct* params, Scripts::CScript*)
{
    int on = 1;
    params->FetchIntValue(0u, &on, 0);
    SetGameModeFlag(kSetTrackTrickScore, on != 0);
    return true;
}

// 0x005AFC00: SetScoreAccumulation <0/1> [freeze_score] (default 1): the game mode's accumulate_score; the score
// frozen only when accumulation is off and freeze_score given.
bool __cdecl CFunc_SetScoreAccumulation(Scripts::CStruct* params, Scripts::CScript*)
{
    int on = 1;
    params->FetchIntValue(0u, &on, 0);
    SetGameModeFlag(kSetAccumulateScore, on != 0);
    SetGameModeFlag(kSetScoreFrozen, !on && params->HasBareFlag(kFreezeScore));
    return true;
}

// 0x005AFC70: AddBonus skater=<n> <points> - a local skater's score's bonus increased.
bool __cdecl CFunc_AddBonus(Scripts::CStruct* params, Scripts::CScript*)
{
    int index = 0;
    params->FetchIntValue(kSkater, &index, 1);
    GameObjects::CGameEntity* skater = GetRiderAt(index);
    if (skater->m_unknownBC) {
        uint8_t* score = ScoreOf(skater);
        int bonus = 0;
        params->FetchIntValue(0u, &bonus, 0);
        AddToBonus(score, bonus);
    }
    return true;
}

// 0x005AFCF0: GetBonusValue skater=<n> - a local skater's bonus as bonus.
bool __cdecl CFunc_GetBonusValue(Scripts::CStruct* params, Scripts::CScript* script)
{
    int index = 0;
    params->FetchIntValue(kSkater, &index, 1);
    GameObjects::CGameEntity* skater = GetRiderAt(index);
    if (skater->m_unknownBC)
        script->m_args->PutInt(kBonus, GetBonus(ScoreOf(skater)));
    return true;
}

// 0x005AFD60: LocalSkaterExists.
bool __cdecl CFunc_LocalSkaterExists(Scripts::CStruct*, Scripts::CScript*) { return GetHomeRider() != nullptr; }

} // namespace ScriptCommands
