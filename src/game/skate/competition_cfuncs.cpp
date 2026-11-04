// Script commands of the game modes' runs: split screen options, competitions, horse, score options, bonuses
// (see competition_cfuncs.h).

#include "game/skate/competition_cfuncs.h"

#include <cstdint>
#include <new>
#include <array>

#include "engine/memory/manager.h"
#include "engine/util/text_store.h"
#include "engine/objects/entity.h"
#include "engine/objects/trick_objects.h"
#include "engine/script/cscript.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "game/skate/contest.h"
#include "game/skate/skate_module_calls.h"
#include "game/rider/rider.h"
#include "game/skate/letter_match.h"
#include "game/skate/point_tally.h"
#include "game/skate/game_mode.h"
#include "engine/sys/game_config.h"
#include "engine/core/down_cast.h"
#include "engine/core/checksums.h"
#include <gsl/pointers>

namespace commands {

namespace {

using namespace skate_calls;

constexpr uint32_t kGameType = 0x9834C4FA;                // game_type
constexpr uint32_t kChecksum = 0x21902065;                // checksum
constexpr uint32_t kTimeLimit = 0xC51BF6E3;               // time_limit
constexpr uint32_t kTime = 0x906B67BA;                    // time
constexpr uint32_t kIsHorse = 0xD1993397;                 // is_horse
constexpr uint32_t kHorseTimeLimit = 0xC146142E;          // horse_time_limit
constexpr uint32_t kHorseWord = 0x6EA5ADDD;               // horse_word
constexpr uint32_t kUiString = 0x96875C0F;                // ui_string
constexpr uint32_t kDefaultTimeLimit = 0x8D38A280;        // default_time_limit
constexpr uint32_t kExtraParams = 0xA61DC9A4;             // extra_params
constexpr uint32_t kWhichString = 0xB8D0CCC8;             // whichString
constexpr uint32_t kId = 0x40C698AF;                      // id
constexpr uint32_t kText = 0xC4745838;                    // text
constexpr uint32_t kCreateHorsePanelMessage = 0x621637CE; // create_horse_panel_message
constexpr uint32_t kFreezeScore = 0x294C84E1;             // freeze_score
constexpr uint32_t kSkater = 0x5B8AB877;                  // skater
constexpr uint32_t kBonus = 0x60678085;                   // bonus

// The skate module's split screen preferences (0x00564D80: +0x58) and its time limit (0x00564D50: +0x370).
prefs::Preferences* split_screen_preferences() { return modules::Skate::sole()->get_split_view_settings(); }
void set_countdown_seconds(int seconds) { modules::Skate::sole()->set_unknown370(seconds); }

// The game mode: 0x00553AD0 the mode `type` loaded (its structure from the mode list); 0x00553BE0 its options
// overridden by `options`; 0x00553000 / 0x00553020 / 0x00553040 / 0x00553060 its accumulate_score / score_frozen /
// degrade_score / track_trick_score.
void load_game_mode(uint32_t type) { static_cast<goals::MatchRules*>(get_game_mode())->apply_match_style(type); }
void override_game_mode_options(script::Struct* options)
{
    static_cast<goals::MatchRules*>(get_game_mode())->force_match_settings(options);
}
void set_game_mode_flag(void (goals::MatchRules::*setter)(bool), bool on)
{
    (static_cast<goals::MatchRules*>(get_game_mode())->*setter)(on);
}
constexpr auto kSetAccumulateScore = &goals::MatchRules::set_score_accumulation;
constexpr auto kApplyPointHold = &goals::MatchRules::hold_points;
constexpr auto kSetDegradeScore = &goals::MatchRules::set_score_degradation;
constexpr auto kSetTrackTrickScore = &goals::MatchRules::set_trick_score_tracking;

// The competition (modules::Contest, no source for these yet): 0x00550560 started with the medal scores and the
// bail penalty; 0x00550EF0 a run ended with the local skater's score; 0x00550B50 the standings sorted; 0x005501A0
// the place is `place`; 0x005501C0 the round is `round` (the runs + 1); 0x005501E0 ended; 0x005501F0 whether ended.
modules::Contest* the_competition() { return get_competition(); }

// The horse game (no source yet): 0x00560100 started; 0x00560180 a run started; 0x00560720 a run ended; 0x005608B0
// the next player; 0x00560380 whether it's over; 0x00560190 its status is the one `params` names; 0x00560440 one of
// its strings (`which`); 0x005601F0 its word (H-O-R-S-E) set. The current skater's id is its +0x04 (0x005480C0).
modules::LetterMatch* the_horse() { return get_horse(); }
uint32_t current_horse_skater() { return static_cast<uint32_t>(get_horse()->skater()); }
// The horse's string `which`, built in `storage` (the caller destroys it).
gsl::owner<text::Chars*> get_horse_string(void* storage, uint32_t which)
{
    return new (storage) text::Chars(the_horse()->fetch_text(which));
}

// A skater's score (Rider::get_points_total): 0x00561B40 its total; 0x00561410 / 0x00561420 its bonus (+0x28) added to /
// read.
modules::PointTally* get_score_of(objects::GameEntity* skater)
{
    return core::down_cast<objects::Rider>(skater)->get_points_total();
}
int total_score(modules::PointTally* score) { return score->get_total_score(); }
void add_to_bonus(modules::PointTally* score, int bonus) { score->add_bonus(bonus); }
int get_bonus(modules::PointTally* score) { return score->get_bonus(); }

// The skater info heap, current while the competition's standings change.
void push_skater_info_heap()
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->skater_info_heap());
}
void pop_heap() { memory::Hub::get_pull()->leave_heap_scope(); }

} // namespace

// 0x005AF090: ApplySplitScreenOptions - the split screen preferences' game type loaded into the game mode; for horse
// its time limit and word applied too (the game mode's default_time_limit and the skate module's time limit). (The
// time_limit preference is read and not used.)
bool cfunc_apply_split_screen_options(script::Struct* /*params*/, script::Script* /*script*/)
{
    prefs::Preferences* prefs = split_screen_preferences();
    uint32_t type = 0;
    prefs->fetch_setting(kGameType)->read_hash_value(kChecksum, &type, 1);
    load_game_mode(type);
    prefs->get_setting_value({.preference = kTimeLimit, .field = kTime});
    if (game_mode_is_true(kIsHorse)) {
        const int time_limit = prefs->get_setting_value({.preference = kHorseTimeLimit, .field = kTime});
        const char* word = prefs->get_preference_string({.preference = kHorseWord, .field = kUiString});
        // (the string is passed by value: the horse's code destroys it)
        the_horse()->set_letters(text::Chars(word));
        const script::StructPtr options = script::make_struct();
        options->add_component_of_type(kDefaultTimeLimit, script::QbValueKind::kInteger, static_cast<uint32_t>(time_limit));
        override_game_mode_options(options.get());
        set_countdown_seconds(time_limit);
    }
    return true;
}

// 0x005AF200: StartCompetition gold= silver= bronze= gold_score= silver_score= bronze_score= bail= [extra_params=]
// - a competition started (in the skater info heap), the extra parameters added to the competition's first.
bool cfunc_start_competition(script::Struct* params, script::Script* /*script*/)
{
    // (uninitialised in the original when missing)
    float gold = 0.0F;
    float silver = 0.0F;
    float bronze = 0.0F;
    float gold_score = 0.0F;
    float silver_score = 0.0F;
    float bronze_score = 0.0F;
    float bail = 0.0F;
    params->fetch_float("gold", &gold, 0);
    params->fetch_float("silver", &silver, 0);
    params->fetch_float("bronze", &bronze, 0);
    params->fetch_float("gold_score", &gold_score, 0);
    params->fetch_float("silver_score", &silver_score, 0);
    params->fetch_float("bronze_score", &bronze_score, 0);
    params->fetch_float("bail", &bail, 0);
    script::Struct* extra = nullptr;
    params->read_sub_struct(kExtraParams, &extra, 0);
    push_skater_info_heap();
    if (extra) {
        the_competition()->rewrite_args(extra);
    }
    the_competition()->kickoff({.bronze = bronze, .silver = silver, .gold = gold, .bronze_score_ = bronze_score, .silver_score_ = silver_score, .gold_score_ = gold_score, .bail_penalty_ = bail});
    pop_heap();
    return true;
}

// 0x005AF320: StartCompetitionRun.
bool cfunc_start_competition_run(script::Struct* /*params*/, script::Script* /*script*/)
{
    the_competition()->begin_timed_session();
    return true;
}

// 0x005AF340: EndCompetitionRun - the local skater's total score ends the run; the standings sorted (in the skater
// info heap).
bool cfunc_end_competition_run(script::Struct* /*params*/, script::Script* /*script*/)
{
    const int score = total_score(get_score_of(get_home_rider()));
    push_skater_info_heap();
    the_competition()->end_run(score);
    the_competition()->show_leader_board();
    pop_heap();
    return true;
}

// 0x005AF3A0 / 0x005AF3D0: PlaceIs <n> / RoundIs <n>.
bool cfunc_place_is(script::Struct* params, script::Script* /*script*/)
{
    int place = 0; // (uninitialised in the original)
    params->fetch_int_value(0U, &place, 0);
    return the_competition()->place_is(place);
}

bool cfunc_round_is(script::Struct* params, script::Script* /*script*/)
{
    int round = 0; // (uninitialised in the original)
    params->fetch_int_value(0U, &round, 0);
    return the_competition()->round_is(round);
}

// 0x005AF400: EndCompetition (in the skater info heap). 0x005AF440: CompetitionEnded.
bool cfunc_end_competition(script::Struct* /*params*/, script::Script* /*script*/)
{
    push_skater_info_heap();
    the_competition()->get_end();
    pop_heap();
    return true;
}

bool cfunc_competition_ended(script::Struct* /*params*/, script::Script* /*script*/)
{
    return the_competition()->is_ended();
}

// 0x005AF450 StartHorse, 0x005AF470 HorseEnded, 0x005AF480 HorseStatusEquals <status>, 0x005AF4A0 StartHorseRun,
// 0x005AF4C0 EndHorseRun, 0x005AF4E0 SwitchHorsePlayers.
bool cfunc_start_horse(script::Struct* /*params*/, script::Script* /*script*/)
{
    the_horse()->prepare_all();
    return true;
}

bool cfunc_horse_ended(script::Struct* /*params*/, script::Script* /*script*/) { return the_horse()->is_over(); }

bool cfunc_horse_status_equals(script::Struct* params, script::Script* /*script*/)
{
    return the_horse()->state_matches(params);
}

bool cfunc_start_horse_run(script::Struct* /*params*/, script::Script* /*script*/)
{
    the_horse()->begin_timed_session();
    return true;
}

bool cfunc_end_horse_run(script::Struct* /*params*/, script::Script* /*script*/)
{
    the_horse()->end_run();
    return true;
}

bool cfunc_switch_horse_players(script::Struct* /*params*/, script::Script* /*script*/)
{
    the_horse()->swap_riders();
    return true;
}

// 0x005AF500: ApplyToHorsePanelString whichString=<s> id=<id> ... - create_horse_panel_message run on the current
// horse skater with the parameters, the string as text and the id.
bool cfunc_apply_to_horse_panel_string(script::Struct* params, script::Script* /*script*/)
{
    uint32_t which = 0;
    uint32_t id = 0;
    params->read_hash_value(kWhichString, &which, 1);
    params->read_hash_value(kId, &id, 1);
    alignas(4) std::array<uint8_t, sizeof(text::Chars)> storage{};
    gsl::owner<text::Chars*> text = get_horse_string(storage.data(), which);
    objects::GameEntity* skater = get_rider_by_number(current_horse_skater());
    script::StructPtr panel = script::make_struct();
    panel->merge_structure(params);
    panel->append_word(kText, text->c_str());
    panel->put_name_key(checksums::Name{kId}, id);
    script::launch_script(kCreateHorsePanelMessage, panel.get(), skater, false, nullptr);
    panel.reset();
    text->~Chars();
    return true;
}

// 0x005AF660: GetHorseString whichString=<s> - the horse's string as text.
bool cfunc_get_horse_string(script::Struct* params, script::Script* script)
{
    uint32_t which = 0;
    params->read_hash_value(kWhichString, &which, 1);
    alignas(4) std::array<uint8_t, sizeof(text::Chars)> storage{};
    gsl::owner<text::Chars*> text = get_horse_string(storage.data(), which);
    script->args()->append_word(kText, text->c_str());
    text->~Chars();
    return true;
}

// 0x005AF700: IsCurrentHorseSkater <id> - whether the current horse skater's id is the integer (or checksum) given.
bool cfunc_is_current_horse_skater(script::Struct* params, script::Script* /*script*/)
{
    int id = 0; // (uninitialised in the original)
    if (params->fetch_int_value(0U, &id, 0)) {
        return current_horse_skater() == static_cast<uint32_t>(id);
    }
    uint32_t name = 0; // (read into the same slot in the original: 0 here as the integer was)
    params->read_hash_value(0U, &name, 1);
    return current_horse_skater() == name;
}

// 0x005AFB40: TrickOffAllObjects <player> - the trick objects nobody holds given to the player (not with the debug
// checks flag 0x00684FA8 set).
bool cfunc_trick_off_all_objects(script::Struct* params, script::Script* /*script*/)
{
    if (!game_config::from_cd()) {
        int player = 0;
        params->fetch_int_value(0U, &player, 0);
        objects::trick_spot_hub()->give_free_clusters(player);
    }
    return true;
}

// 0x005AFB80 / 0x005AFBC0: SetScoreDegradation <0/1> / SetTrickScoreTracking <0/1> (default 1): the game mode's
// degrade_score / track_trick_score.
bool cfunc_set_score_degradation(script::Struct* params, script::Script* /*script*/)
{
    int on = 1;
    params->fetch_int_value(0U, &on, 0);
    set_game_mode_flag(kSetDegradeScore, on != 0);
    return true;
}

bool cfunc_set_trick_score_tracking(script::Struct* params, script::Script* /*script*/)
{
    int on = 1;
    params->fetch_int_value(0U, &on, 0);
    set_game_mode_flag(kSetTrackTrickScore, on != 0);
    return true;
}

// 0x005AFC00: SetScoreAccumulation <0/1> [freeze_score] (default 1): the game mode's accumulate_score; the score
// frozen only when accumulation is off and freeze_score given.
bool cfunc_set_score_accumulation(script::Struct* params, script::Script* /*script*/)
{
    int on = 1;
    params->fetch_int_value(0U, &on, 0);
    set_game_mode_flag(kSetAccumulateScore, on != 0);
    set_game_mode_flag(kApplyPointHold, (on == 0) && params->has_bare_flag(kFreezeScore));
    return true;
}

// 0x005AFC70: AddBonus skater=<n> <points> - a local skater's score's bonus increased.
bool cfunc_add_bonus(script::Struct* params, script::Script* /*script*/)
{
    int index = 0;
    params->fetch_int_value(kSkater, &index, 1);
    objects::GameEntity* skater = get_rider_at(index);
    if (skater->is_local_skater() != 0U) {
        modules::PointTally* score = get_score_of(skater);
        int bonus = 0;
        params->fetch_int_value(0U, &bonus, 0);
        add_to_bonus(score, bonus);
    }
    return true;
}

// 0x005AFCF0: GetBonusValue skater=<n> - a local skater's bonus as bonus.
bool cfunc_get_bonus_value(script::Struct* params, script::Script* script)
{
    int index = 0;
    params->fetch_int_value(kSkater, &index, 1);
    objects::GameEntity* skater = get_rider_at(index);
    if (skater->is_local_skater() != 0U) {
        script->args()->put_int(checksums::Name{kBonus}, get_bonus(get_score_of(skater)));
    }
    return true;
}

// 0x005AFD60: LocalSkaterExists.
bool cfunc_local_skater_exists(script::Struct* /*params*/, script::Script* /*script*/)
{
    return get_home_rider() != nullptr;
}

} // namespace commands
