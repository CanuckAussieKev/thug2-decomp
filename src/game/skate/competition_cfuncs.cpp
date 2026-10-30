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
#include "hook/game.h"
#include "hook/hook.h"

namespace CFuncs {

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
Prefs::Preferences* SplitScreenPreferences()
{
    return Mdl::Skate::Instance()->GetSplitScreenPreferences();
}
void SetTimeLimit(int seconds) { Mdl::Skate::Instance()->SetUnknown370(seconds); }

// The game mode: 0x00553AD0 the mode `type` loaded (its structure from the mode list); 0x00553BE0 its options
// overridden by `options`; 0x00553000 / 0x00553020 / 0x00553040 / 0x00553060 its accumulate_score / score_frozen /
// degrade_score / track_trick_score.
void LoadGameMode(uint32_t type) { static_cast<Game::CGameMode*>(GameMode())->LoadGameType(type); }
void OverrideGameModeOptions(Script::CStruct* options)
{
    static_cast<Game::CGameMode*>(GameMode())->OverrideOptions(options);
}
void SetGameModeFlag(void (Game::CGameMode::*setter)(bool), bool on) { (static_cast<Game::CGameMode*>(GameMode())->*setter)(on); }
constexpr auto kSetAccumulateScore = &Game::CGameMode::SetScoreAccumulation;
constexpr auto kSetScoreFrozen = &Game::CGameMode::SetScoreFrozen;
constexpr auto kSetDegradeScore = &Game::CGameMode::SetScoreDegradation;
constexpr auto kSetTrackTrickScore = &Game::CGameMode::SetTrickScoreTracking;

// The competition (Mdl::CCompetition, no source for these yet): 0x00550560 started with the medal scores and the
// bail penalty; 0x00550EF0 a run ended with the local skater's score; 0x00550B50 the standings sorted; 0x005501A0
// the place is `place`; 0x005501C0 the round is `round` (the runs + 1); 0x005501E0 ended; 0x005501F0 whether ended.
Mdl::CCompetition* TheCompetition() { return reinterpret_cast<Mdl::CCompetition*>(Competition()); }

// The horse game (no source yet): 0x00560100 started; 0x00560180 a run started; 0x00560720 a run ended; 0x005608B0
// the next player; 0x00560380 whether it's over; 0x00560190 its status is the one `params` names; 0x00560440 one of
// its strings (`which`); 0x005601F0 its word (H-O-R-S-E) set. The current skater's id is its +0x04 (0x005480C0).
Mdl::CHorse* TheHorse() { return reinterpret_cast<Mdl::CHorse*>(Horse()); }
uint32_t CurrentHorseSkater() { return *reinterpret_cast<uint32_t*>(Horse() + 4); }
// The horse's string `which`, built in `storage` (the caller destroys it).
Str::String* GetHorseString(void* storage, uint32_t which)
{
    return new (storage) Str::String(TheHorse()->GetString(which));
}

// A skater's score (CSkater::GetScore): 0x00561B40 its total; 0x00561410 / 0x00561420 its bonus (+0x28) added to /
// read.
uint8_t* ScoreOf(Obj::CCompositeObject* skater)
{
    return static_cast<uint8_t*>(static_cast<Obj::CSkater*>(skater)->GetScore());
}
int TotalScore(uint8_t* score) { return reinterpret_cast<Mdl::Score*>(score)->GetTotalScore(); }
void AddToBonus(uint8_t* score, int bonus) { reinterpret_cast<Mdl::Score*>(score)->AddBonus(bonus); }
int GetBonus(uint8_t* score) { return reinterpret_cast<Mdl::Score*>(score)->GetBonus(); }

// The skater info heap, current while the competition's standings change.
void PushSkaterInfoHeap()
{
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_skaterInfoHeap);
}
void PopHeap() { Mem::Manager::Get()->PopContext(); }

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

} // namespace

// 0x005AF090: ApplySplitScreenOptions - the split screen preferences' game type loaded into the game mode; for horse
// its time limit and word applied too (the game mode's default_time_limit and the skate module's time limit). (The
// time_limit preference is read and not used.)
bool __cdecl CFunc_ApplySplitScreenOptions(Script::CStruct*, Script::CScript*)
{
    Prefs::Preferences* prefs = SplitScreenPreferences();
    uint32_t type = 0;
    prefs->GetPreference(kGameType)->GetChecksum(kChecksum, &type, 1);
    LoadGameMode(type);
    prefs->GetPreferenceValue(kTimeLimit, kTime);
    if (GameModeIsTrue(kIsHorse)) {
        int timeLimit = prefs->GetPreferenceValue(kHorseTimeLimit, kTime);
        const char* word = prefs->GetPreferenceString(kHorseWord, kUiString);
        // (the string is passed by value: the horse's code destroys it)
        TheHorse()->SetWord(Str::String(word));
        Script::CStruct* options = NewParams();
        options->AddComponentOfType(kDefaultTimeLimit, Script::ESYMBOLTYPE_INTEGER, static_cast<uint32_t>(timeLimit));
        OverrideGameModeOptions(options);
        SetTimeLimit(timeLimit);
        DeleteParams(options);
    }
    return true;
}

// 0x005AF200: StartCompetition gold= silver= bronze= gold_score= silver_score= bronze_score= bail= [extra_params=]
// - a competition started (in the skater info heap), the extra parameters added to the competition's first.
bool __cdecl CFunc_StartCompetition(Script::CStruct* params, Script::CScript*)
{
    // (uninitialised in the original when missing)
    float gold = 0.0f, silver = 0.0f, bronze = 0.0f, goldScore = 0.0f, silverScore = 0.0f, bronzeScore = 0.0f,
          bail = 0.0f;
    params->GetFloat("gold", &gold, 0);
    params->GetFloat("silver", &silver, 0);
    params->GetFloat("bronze", &bronze, 0);
    params->GetFloat("gold_score", &goldScore, 0);
    params->GetFloat("silver_score", &silverScore, 0);
    params->GetFloat("bronze_score", &bronzeScore, 0);
    params->GetFloat("bail", &bail, 0);
    Script::CStruct* extra = nullptr;
    params->GetStructure(kExtraParams, &extra, 0);
    PushSkaterInfoHeap();
    if (extra)
        TheCompetition()->EditParams(extra);
    TheCompetition()->Start(bronze, silver, gold, bronzeScore, silverScore, goldScore, bail);
    PopHeap();
    return true;
}

// 0x005AF320: StartCompetitionRun.
bool __cdecl CFunc_StartCompetitionRun(Script::CStruct*, Script::CScript*)
{
    TheCompetition()->StartRun();
    return true;
}

// 0x005AF340: EndCompetitionRun - the local skater's total score ends the run; the standings sorted (in the skater
// info heap).
bool __cdecl CFunc_EndCompetitionRun(Script::CStruct*, Script::CScript*)
{
    int score = TotalScore(ScoreOf(GetLocalSkater()));
    PushSkaterInfoHeap();
    TheCompetition()->EndRun(score);
    TheCompetition()->ShowLeaderBoard();
    PopHeap();
    return true;
}

// 0x005AF3A0 / 0x005AF3D0: PlaceIs <n> / RoundIs <n>.
bool __cdecl CFunc_PlaceIs(Script::CStruct* params, Script::CScript*)
{
    int place = 0; // (uninitialised in the original)
    params->GetInteger(0u, &place, 0);
    return TheCompetition()->PlaceIs(place);
}

bool __cdecl CFunc_RoundIs(Script::CStruct* params, Script::CScript*)
{
    int round = 0; // (uninitialised in the original)
    params->GetInteger(0u, &round, 0);
    return TheCompetition()->RoundIs(round);
}

// 0x005AF400: EndCompetition (in the skater info heap). 0x005AF440: CompetitionEnded.
bool __cdecl CFunc_EndCompetition(Script::CStruct*, Script::CScript*)
{
    PushSkaterInfoHeap();
    TheCompetition()->End();
    PopHeap();
    return true;
}

bool __cdecl CFunc_CompetitionEnded(Script::CStruct*, Script::CScript*)
{
    return TheCompetition()->IsEnded();
}

// 0x005AF450 StartHorse, 0x005AF470 HorseEnded, 0x005AF480 HorseStatusEquals <status>, 0x005AF4A0 StartHorseRun,
// 0x005AF4C0 EndHorseRun, 0x005AF4E0 SwitchHorsePlayers.
bool __cdecl CFunc_StartHorse(Script::CStruct*, Script::CScript*)
{
    TheHorse()->Init();
    return true;
}

bool __cdecl CFunc_HorseEnded(Script::CStruct*, Script::CScript*)
{
    return TheHorse()->Ended();
}

bool __cdecl CFunc_HorseStatusEquals(Script::CStruct* params, Script::CScript*)
{
    return TheHorse()->StatusEquals(params);
}

bool __cdecl CFunc_StartHorseRun(Script::CStruct*, Script::CScript*)
{
    TheHorse()->StartRun();
    return true;
}

bool __cdecl CFunc_EndHorseRun(Script::CStruct*, Script::CScript*)
{
    TheHorse()->EndRun();
    return true;
}

bool __cdecl CFunc_SwitchHorsePlayers(Script::CStruct*, Script::CScript*)
{
    TheHorse()->SwitchPlayers();
    return true;
}

// 0x005AF500: ApplyToHorsePanelString whichString=<s> id=<id> ... - create_horse_panel_message run on the current
// horse skater with the parameters, the string as text and the id.
bool __cdecl CFunc_ApplyToHorsePanelString(Script::CStruct* params, Script::CScript*)
{
    uint32_t which = 0, id = 0;
    params->GetChecksum(kWhichString, &which, 1);
    params->GetChecksum(kId, &id, 1);
    alignas(4) uint8_t storage[sizeof(Str::String)];
    Str::String* text = GetHorseString(storage, which);
    Obj::CCompositeObject* skater = GetSkaterById(CurrentHorseSkater());
    Script::CStruct* panel = NewParams();
    panel->AppendStructure(params);
    panel->AddString(kText, text->c_str());
    panel->AddChecksum(kId, id);
    Script::RunScript(kCreateHorsePanelMessage, panel, skater, false, nullptr);
    DeleteParams(panel);
    text->~String();
    return true;
}

// 0x005AF660: GetHorseString whichString=<s> - the horse's string as text.
bool __cdecl CFunc_GetHorseString(Script::CStruct* params, Script::CScript* script)
{
    uint32_t which = 0;
    params->GetChecksum(kWhichString, &which, 1);
    alignas(4) uint8_t storage[sizeof(Str::String)];
    Str::String* text = GetHorseString(storage, which);
    script->m_params->AddString(kText, text->c_str());
    text->~String();
    return true;
}

// 0x005AF700: IsCurrentHorseSkater <id> - whether the current horse skater's id is the integer (or checksum) given.
bool __cdecl CFunc_IsCurrentHorseSkater(Script::CStruct* params, Script::CScript*)
{
    int id = 0; // (uninitialised in the original)
    if (!params->GetInteger(0u, &id, 0))
        params->GetChecksum(0u, reinterpret_cast<uint32_t*>(&id), 1);
    return CurrentHorseSkater() == static_cast<uint32_t>(id);
}

// 0x005AFB40: TrickOffAllObjects <player> - the trick objects nobody holds given to the player (not with the debug
// checks flag 0x00684FA8 set).
bool __cdecl CFunc_TrickOffAllObjects(Script::CStruct* params, Script::CScript*)
{
    if (!GameVariable<bool>(0x00684FA8)) {
        int player = 0;
        params->GetInteger(0u, &player, 0);
        GameVariable<Obj::CTrickObjectManager*>(0x007CE500)->GiveFreeClusters(player);
    }
    return true;
}

// 0x005AFB80 / 0x005AFBC0: SetScoreDegradation <0/1> / SetTrickScoreTracking <0/1> (default 1): the game mode's
// degrade_score / track_trick_score.
bool __cdecl CFunc_SetScoreDegradation(Script::CStruct* params, Script::CScript*)
{
    int on = 1;
    params->GetInteger(0u, &on, 0);
    SetGameModeFlag(kSetDegradeScore, on != 0);
    return true;
}

bool __cdecl CFunc_SetTrickScoreTracking(Script::CStruct* params, Script::CScript*)
{
    int on = 1;
    params->GetInteger(0u, &on, 0);
    SetGameModeFlag(kSetTrackTrickScore, on != 0);
    return true;
}

// 0x005AFC00: SetScoreAccumulation <0/1> [freeze_score] (default 1): the game mode's accumulate_score; the score
// frozen only when accumulation is off and freeze_score given.
bool __cdecl CFunc_SetScoreAccumulation(Script::CStruct* params, Script::CScript*)
{
    int on = 1;
    params->GetInteger(0u, &on, 0);
    SetGameModeFlag(kSetAccumulateScore, on != 0);
    SetGameModeFlag(kSetScoreFrozen, !on && params->ContainsFlag(kFreezeScore));
    return true;
}

// 0x005AFC70: AddBonus skater=<n> <points> - a local skater's score's bonus increased.
bool __cdecl CFunc_AddBonus(Script::CStruct* params, Script::CScript*)
{
    int index = 0;
    params->GetInteger(kSkater, &index, 1);
    Obj::CCompositeObject* skater = GetSkater(index);
    if (skater->m_unknownBC) {
        uint8_t* score = ScoreOf(skater);
        int bonus = 0;
        params->GetInteger(0u, &bonus, 0);
        AddToBonus(score, bonus);
    }
    return true;
}

// 0x005AFCF0: GetBonusValue skater=<n> - a local skater's bonus as bonus.
bool __cdecl CFunc_GetBonusValue(Script::CStruct* params, Script::CScript* script)
{
    int index = 0;
    params->GetInteger(kSkater, &index, 1);
    Obj::CCompositeObject* skater = GetSkater(index);
    if (skater->m_unknownBC)
        script->m_params->AddInteger(kBonus, GetBonus(ScoreOf(skater)));
    return true;
}

// 0x005AFD60: LocalSkaterExists.
bool __cdecl CFunc_LocalSkaterExists(Script::CStruct*, Script::CScript*) { return GetLocalSkater() != nullptr; }

} // namespace CFuncs

REPLACE(0x005AF090, CFuncs::CFunc_ApplySplitScreenOptions);
REPLACE(0x005AF200, CFuncs::CFunc_StartCompetition);
REPLACE(0x005AF320, CFuncs::CFunc_StartCompetitionRun);
REPLACE(0x005AF340, CFuncs::CFunc_EndCompetitionRun);
REPLACE(0x005AF3A0, CFuncs::CFunc_PlaceIs);
REPLACE(0x005AF3D0, CFuncs::CFunc_RoundIs);
REPLACE(0x005AF400, CFuncs::CFunc_EndCompetition);
REPLACE(0x005AF440, CFuncs::CFunc_CompetitionEnded);
REPLACE(0x005AF450, CFuncs::CFunc_StartHorse);
REPLACE(0x005AF470, CFuncs::CFunc_HorseEnded);
REPLACE(0x005AF480, CFuncs::CFunc_HorseStatusEquals);
REPLACE(0x005AF4A0, CFuncs::CFunc_StartHorseRun);
REPLACE(0x005AF4C0, CFuncs::CFunc_EndHorseRun);
REPLACE(0x005AF4E0, CFuncs::CFunc_SwitchHorsePlayers);
REPLACE(0x005AF500, CFuncs::CFunc_ApplyToHorsePanelString);
REPLACE(0x005AF660, CFuncs::CFunc_GetHorseString);
REPLACE(0x005AF700, CFuncs::CFunc_IsCurrentHorseSkater);
REPLACE(0x005AFB40, CFuncs::CFunc_TrickOffAllObjects);
REPLACE(0x005AFB80, CFuncs::CFunc_SetScoreDegradation);
REPLACE(0x005AFBC0, CFuncs::CFunc_SetTrickScoreTracking);
REPLACE(0x005AFC00, CFuncs::CFunc_SetScoreAccumulation);
REPLACE(0x005AFC70, CFuncs::CFunc_AddBonus);
REPLACE(0x005AFCF0, CFuncs::CFunc_GetBonusValue);
REPLACE(0x005AFD60, CFuncs::CFunc_LocalSkaterExists);
