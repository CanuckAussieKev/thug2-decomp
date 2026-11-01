// goals::MatchRules and goals::MatchFlow: 0x00552E10-0x00553DF9 (see game_mode.h).

#include <cstdint>
#include <initializer_list>
#include <new>

#include "game/skate/game_mode.h"

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "game/goals/objective.h"
#include "engine/net/gamenet_manager.h"
#include "engine/script/cscript.h"
#include "engine/script/preferences.h"
#include "game/skate/skate_module_calls.h"
#include "game/skate/victory_conditions.h"
#include "game/goals/goal_manager.h"

namespace goals {

goals::MatchRules* g_game_mode;
uint32_t g_game_mode_references;

namespace {

constexpr uint32_t kAccumulateScore = 0xC1F5864B;    // accumulate_score
constexpr uint32_t kTrackBestCombo = 0xD57C16BA;     // track_best_combo
constexpr uint32_t kPointsHeldFlag = 0x78FB9F04;     // score_frozen
constexpr uint32_t kDegradeScore = 0xCA37E639;       // degrade_score
constexpr uint32_t kTrackTrickScore = 0x47F763FF;    // track_trick_score
constexpr uint32_t kTeamGraffitiMode = 0xA8774BF7;   // team_graffiti_mode
constexpr uint32_t kGoals = 0x38DBE1D0;              // goals
constexpr uint32_t kNumTeams = 0x2EBE6CD1;           // num_teams
constexpr uint32_t kTimerBeeps = 0x035C8073;         // timer_beeps
constexpr uint32_t kDefaultTimeLimit = 0x8D38A280;   // default_time_limit
constexpr uint32_t kInitialPlayers = 0x6DC3D6F1;     // initial_players
constexpr uint32_t kMaxPlayers = 0x4D42E61D;         // max_players
constexpr uint32_t kFireballLevel = 0xCE87E4E3;      // fireball_level
constexpr uint32_t kRound = 0x3A1115CB;              // round
constexpr uint32_t kName = 0xA1DC81F9;               // Name
constexpr uint32_t kKing = 0x5D32129C;               // king
constexpr uint32_t kNetKing = 0x6EF8FDA0;            // netking
constexpr uint32_t kScoreChallenge = 0xF135ECB6;     // scorechallenge
constexpr uint32_t kNetScoreChallenge = 0x1498240A;  // netscorechallenge
constexpr uint32_t kCreateGoals = 0xE45F0CA2;        // creategoals
constexpr uint32_t kFirefight = 0x3D6D444F;          // firefight
constexpr uint32_t kNetFirefight = 0xBFF33600;       // netfirefight
constexpr uint32_t kStopAtZero = 0x48E748B5;         // stop_at_zero
constexpr uint32_t kNetLobby = 0x1C471C60;           // NetLobby
constexpr uint32_t kScreenMode = 0x9BAB2EAE;         // screenmode
constexpr uint32_t kSplitScreen = 0x06AB02F2;        // splitscreen
constexpr uint32_t kSingle = 0x3558D8E6;             // single
constexpr uint32_t kHorse = 0x9D65D0E7;              // Horse
constexpr uint32_t kScreenLayoutKey = 0x6F82C71E;    // viewport_type
constexpr uint32_t kChecksum = 0x21902065;           // checksum
constexpr uint32_t kViewportVertical = 0x92F5B4CB;   // viewport_type_vertical
constexpr uint32_t kViewportHorizontal = 0x7C442511; // viewport_type_horizontal
constexpr uint32_t kVictoryConditions = 0x94A24C5E;  // victory_conditions
constexpr uint32_t kIsElimiskate = 0x2B1304A1;       // is_elimiskate
constexpr uint32_t kIsScavengerHunt = 0xD4734865;    // is_scavenger_hunt
constexpr uint32_t kModeInfo = 0xCA5F7FFB;           // mode_info
constexpr uint32_t kDefinition = 0x97CFD027;         // definition
constexpr uint32_t kFreeskate = 0x34861A16;          // freeskate

WinRule* const kHeadMarker = reinterpret_cast<WinRule*>(-1);

// (the getters' local when an asserted integer isn't there: the original's is the pushed `this`)
int unset(const void* mode) { return static_cast<int>(reinterpret_cast<uintptr_t>(mode)); }

MatchRules*& the_game_mode() { return g_game_mode; }
uint32_t& ref_tally() { return g_game_mode_references; }
online::Hub* net_session_slot() { return online::g_online_hub; }

// The goal manager (0x005591B0): 0x0055DC30 how many goals (`active`...) and 0x0055E980 its current net goal.
void* objective_board() { return get_objective_board(); }
int count_goals(void* manager, int which) { return static_cast<GoalManager*>(manager)->count_live_goals(which != 0); }
Objective* current_net_goal(void* manager) { return static_cast<GoalManager*>(manager)->get_active_net_goal(); }
Objective* get_goal(void* manager, uint32_t id, int assert)
{
    return static_cast<GoalManager*>(manager)->get_goal(id, assert);
}

// The conditions' list: its length and its `index`th node (lists::Foremost's, inlined by the original: each counts from
// the start).
uint32_t count_conditions(MatchRules::ConditionNode* head)
{
    uint32_t count = 0;
    MatchRules::ConditionNode* node = head->succ_;
    if (node->condition_ == kHeadMarker || !node)
        return 0;
    do {
        node = node->succ_;
        ++count;
    } while (node->condition_ != kHeadMarker && node);
    return count;
}
MatchRules::ConditionNode* condition_at(MatchRules::ConditionNode* head, uint32_t index)
{
    MatchRules::ConditionNode* node = head->succ_;
    if (node->condition_ == kHeadMarker || !node)
        return nullptr;
    for (uint32_t left = index; left; --left) {
        node = node->succ_;
        if (node->condition_ == kHeadMarker || !node)
            return nullptr;
    }
    return node;
}

} // namespace

// --- The game flow -------------------------------------------------------------------------------------------

// 0x00552F30
MatchFlow::~MatchFlow()
{
    script::Script::destroy(own_script_);
}

// 0x00552E30
void MatchFlow::get_request(uint32_t name)
{
    label_ = name;
    pending_ = true;
}

// 0x00552E40
void MatchFlow::tick_frame()
{
    if (pending_) {
        pending_ = false;
        script::Script::destroy(own_script_);
        auto* script = script::Script::create();
        own_script_ = script;
        script->bind_script(label_, nullptr, nullptr);
    }
    if (own_script_ && static_cast<int>(own_script_->tick_frame()) == 0) {
        script::Script::destroy(own_script_);
        own_script_ = nullptr;
    }
}

// --- The game mode -------------------------------------------------------------------------------------------

// 0x00553DB0
MatchRules* MatchRules::sole(bool create)
{
    MatchRules* mode = the_game_mode();
    if (!mode && create) {
        mode = new MatchRules;
        the_game_mode() = mode;
    }
    ++ref_tally();
    return mode;
}

// 0x00552F70
void MatchRules::get_release()
{
    if (ref_tally() > 1) {
        --ref_tally();
        return;
    }
    delete the_game_mode();
    the_game_mode() = nullptr;
    ref_tally() = 0;
}

// 0x00553D30
MatchRules::MatchRules()
{
    conditions_.condition_ = kHeadMarker;
    conditions_.rank_ = 0;
    conditions_.succ_ = &conditions_;
    conditions_.last_value_ = &conditions_;
    clear_victory_conditions();
    apply_match_style(kFreeskate);
}

// 0x00553890 / 0x00553CE0
MatchRules::~MatchRules()
{
    clear_victory_conditions();
    conditions_.unlink();
    args_.get_clear();
}

// 0x00553710
void MatchRules::ConditionNode::unlink()
{
    last_value_->succ_ = succ_;
    succ_->last_value_ = last_value_;
    succ_ = this;
    last_value_ = this;
}

// 0x00553D10
void MatchRules::get_reset()
{
    clear_victory_conditions();
    apply_match_style(kFreeskate);
}

// 0x00553770
void MatchRules::clear_victory_conditions()
{
    ConditionNode* node = conditions_.succ_;
    if (node->condition_ == kHeadMarker || !node)
        return;
    while (node) {
        ConditionNode* next = node->succ_->condition_ == kHeadMarker ? nullptr : node->succ_;
        node->last_value_->succ_ = node->succ_;
        node->succ_->last_value_ = node->last_value_;
        node->succ_ = node;
        node->last_value_ = node;
        delete node->condition_;
        memory::dealloc(node); // (its destructor only unlinks it again)
        node = next;
    }
}

// 0x005537D0
void MatchRules::build_victory_conditions()
{
    clear_victory_conditions();
    script::Array* conditions;
    if (!args_.fetch_list(kVictoryConditions, &conditions, 0))
        return;
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->skater_info_heap_);
    for (uint32_t i = 0; i < conditions->extent_; ++i) {
        WinRule* condition = create_victory_condition(reinterpret_cast<script::Struct*>(conditions->fetch_part(i)));
        auto* node = static_cast<ConditionNode*>(memory::alloc_zeroed(sizeof(ConditionNode)));
        if (node) {
            node->condition_ = condition;
            node->rank_ = 0;
            node->succ_ = node;
            node->last_value_ = node;
        }
        // (at the end of the list; a failed allocation writes through null, as the original)
        node->last_value_ = conditions_.last_value_;
        node->succ_ = &conditions_;
        conditions_.last_value_->succ_ = node;
        conditions_.last_value_ = node;
    }
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x00553910
bool MatchRules::finish_rules_reached()
{
    int elimiskate = 0;
    args_.fetch_int_value(kIsElimiskate, &elimiskate, 0);
    bool is_elimiskate = elimiskate == 1;
    int scavenger = 0;
    args_.fetch_int_value(kIsScavengerHunt, &scavenger, 0);
    bool is_scavenger_hunt = scavenger == 1;
    if (uses_countdown() && !is_elimiskate && !is_scavenger_hunt) {
        void* manager = objective_board();
        if (count_goals(manager, 1) > 0) {
            Objective* goal = current_net_goal(manager);
            if (goal && goal->get_seconds_remaining() <= 0)
                return true;
        }
    }
    int terminal = 0;
    int complete = 0;
    for (uint32_t i = 0; i < count_conditions(&conditions_); ++i) {
        // (a node past the list would be read through null, as the original)
        WinRule* condition = condition_at(&conditions_, i)->condition_;
        if (condition->is_terminal()) {
            ++terminal;
            if (condition->rule_satisfied())
                ++complete;
        }
    }
    return terminal && complete == terminal;
}

// 0x00553A60
int MatchRules::get_first_condition_value()
{
    for (uint32_t i = 0; i < count_conditions(&conditions_); ++i) {
        WinRule* condition = condition_at(&conditions_, i)->condition_;
        if (condition)
            return condition->target_value();
    }
    return 0;
}

// 0x00553AD0
bool MatchRules::apply_match_style(uint32_t type)
{
    int max_players = 8;
    online::Hub* net = net_session_slot();
    if (net && net->in_net_game())
        args_.fetch_int_value(kMaxPlayers, &max_players, true);
    args_.get_clear();
    script::Array* modes = script::fetch_list(kModeInfo);
    for (uint32_t i = 0; i < modes->extent_; ++i) {
        auto* mode = reinterpret_cast<script::Struct*>(modes->fetch_part(i));
        uint32_t name;
        mode->read_hash_value(kChecksum, &name, true);
        if (name != type)
            continue;
        const char* definition;
        mode->fetch_text(kDefinition, &definition, true);
        args_.append_structure_if_any(script::read_sub_struct(definition));
        clear_victory_conditions();
        build_victory_conditions();
        if (net && net->in_net_game())
            args_.put_int(kMaxPlayers, max_players);
        return true;
    }
    return false;
}

// 0x00553BE0
bool MatchRules::force_match_settings(script::Struct* options)
{
    for (const char* name : {"time_limit_type",        // "time_limit_type"
                             "victory_condition_type", // "victory_condition_type"
                             "screenmode"}) {          // "screenmode"
        if (options->has_entry_named(name))
            args_.erase_module(checksums::hash_from_text(name));
    }
    args_.append_structure_if_any(options);
    clear_victory_conditions();
    build_victory_conditions();
    script::dump_to_log(&args_, 0);
    return true;
}

// 0x00552FB0
bool MatchRules::keeps_running_total()
{
    int accumulate = unset(this);
    args_.fetch_int_value(kAccumulateScore, &accumulate, true);
    return accumulate != 0;
}

// 0x00552FD0
bool MatchRules::keeps_best_chain()
{
    int track;
    if (!args_.fetch_int_value(kTrackBestCombo, &track, false))
        return false;
    return track != 0;
}

// 0x00553000
void MatchRules::set_score_accumulation(bool on) { args_.put_int(kAccumulateScore, on); }

// 0x00553020
void MatchRules::hold_points(bool on) { args_.put_int(kPointsHeldFlag, on); }

// 0x00553040
void MatchRules::set_score_degradation(bool on) { args_.put_int(kDegradeScore, on); }

// 0x00553060
void MatchRules::set_trick_score_tracking(bool on) { args_.put_int(kTrackTrickScore, on); }

// 0x00553080
void MatchRules::set_team_graffiti_mode(uint32_t mode) { args_.put_name_key(kTeamGraffitiMode, mode); }

// 0x005530A0
void MatchRules::set_goals_mode(uint32_t mode) { args_.put_name_key(kGoals, mode); }

// 0x005530C0
int MatchRules::is_scoring_paused()
{
    int frozen = unset(this);
    args_.fetch_int_value(kPointsHeldFlag, &frozen, true);
    return frozen != 0 ? 1 : 0;
}

// 0x005530F0
bool MatchRules::keeps_move_points()
{
    int track = unset(this);
    args_.fetch_int_value(kTrackTrickScore, &track, true);
    return track != 0;
}

// 0x00553110
bool MatchRules::repeats_lose_points()
{
    int degrade = unset(this);
    args_.fetch_int_value(kDegradeScore, &degrade, true);
    return degrade != 0;
}

// 0x00553130
int MatchRules::current_num_teams()
{
    int teams = unset(this);
    args_.fetch_int_value(kNumTeams, &teams, true);
    return teams;
}

// 0x00553150
bool MatchRules::clock_beeps_near_end()
{
    int beeps = unset(this);
    args_.fetch_int_value(kTimerBeeps, &beeps, true);
    return beeps != 0;
}

// 0x00553170
int MatchRules::get_time_limit()
{
    int limit = unset(this);
    args_.fetch_int_value(kDefaultTimeLimit, &limit, true);
    return limit;
}

// 0x00553190
int MatchRules::get_starting_rider_count()
{
    int players = unset(this);
    args_.fetch_int_value(kInitialPlayers, &players, true);
    return players;
}

// 0x005531B0
void MatchRules::set_rider_ceiling(int players) { args_.put_int(kMaxPlayers, players); }

// 0x005531D0
int MatchRules::get_rider_ceiling()
{
    int players = unset(this);
    args_.fetch_int_value(kMaxPlayers, &players, true);
    return players;
}

// 0x005531F0
int MatchRules::get_fireball_level()
{
    int level = 3;
    args_.fetch_int_value(kFireballLevel, &level, 0);
    return level;
}

// 0x00553220
bool MatchRules::is_true(uint32_t name)
{
    int value = 0;
    args_.fetch_int_value(name, &value, false);
    return value != 0;
}

// 0x00553250
void MatchRules::set_round(int round) { args_.put_int(kRound, round); }

// 0x00553270
int MatchRules::get_round()
{
    int round = 0;
    args_.fetch_int_value(kRound, &round, false);
    return round;
}

// 0x005532A0
uint32_t MatchRules::get_name_key()
{
    uint32_t name = 0;
    args_.read_hash_value(kName, &name, 0);
    return name;
}

// 0x005532D0
uint32_t MatchRules::get_goals_mode()
{
    uint32_t mode = 0;
    args_.read_hash_value(kGoals, &mode, 0);
    return mode;
}

// 0x00553300
uint32_t MatchRules::get_team_graffiti_mode()
{
    uint32_t mode = 0;
    args_.read_hash_value(kTeamGraffitiMode, &mode, 0);
    return mode;
}

// 0x00553330
uint32_t MatchRules::remap_online_name_key(uint32_t name)
{
    switch (name) {
    case 0x30C2FFA3:
        return 0xF9283EE7; // nettrickattack: trickattack
    case 0x5E2EA50C:
        return 0xC8A82B5A; // netgraffiti: Graffiti
    case 0xC50AFFD0:
        return 0x23EB3DA3; // netcombomambo: combomambo
    case 0xF9D5D933:
        return 0xCA1F360F; // netslap: slap
    case kNetKing:
        return kKing;
    case kNetFirefight:
        return kFirefight;
    case 0x6C5FF266:
        return 0xA5AD2B0B; // netctf: CTF
    case 0x10F84276:
        return 0xF6198005; // netelimiskate: elimiskate
    default:
        return name;
    }
}

// 0x005533B0
int MatchRules::get_goal_time_left()
{
    void* manager = objective_board();
    uint32_t name = 0;
    args_.read_hash_value(kName, &name, 0);
    Objective* goal = get_goal(manager, remap_online_name_key(name), 0);
    return goal ? goal->get_seconds_remaining() : 0;
}

// 0x00553400
void MatchRules::set_goal_time_left(int seconds)
{
    void* manager = objective_board();
    uint32_t name = 0;
    args_.read_hash_value(kName, &name, 0);
    Objective* goal = get_goal(manager, remap_online_name_key(name), 0);
    if (goal)
        goal->set_seconds_remaining(seconds);
}

// 0x00553460
bool MatchRules::uses_countdown()
{
    // (the name looked up again for each of the first four)
    uint32_t name = 0;
    for (uint32_t clockless : {kKing, kNetKing, kScoreChallenge, kNetScoreChallenge}) {
        name = 0;
        args_.read_hash_value(kName, &name, 0);
        if (name == clockless)
            return false;
    }
    if (get_name_key() == kCreateGoals || get_name_key() == kFirefight || get_name_key() == kNetFirefight)
        return false;
    int limit = static_cast<int>(name); // (the original's local is the name's)
    args_.fetch_int_value(kDefaultTimeLimit, &limit, true);
    return limit != 0;
}

// 0x00553550
bool MatchRules::clock_halts_at_zero()
{
    if (!modules::Skate::is_multiplayer()) // (the skate module's is_versus_session)
        return false;
    if (!uses_countdown())
        return true;
    int stop = unset(this);
    args_.fetch_int_value(kStopAtZero, &stop, true);
    return stop == 1;
}

// 0x005535A0
void MatchRules::set_num_teams(int teams)
{
    online::Hub* net = net_session_slot();
    args_.put_int(kNumTeams, teams);
    uint32_t name = 0;
    args_.read_hash_value(kName, &name, 0);
    if (name == kNetLobby)
        net->build_team_flag_set(teams);
}

// 0x005535F0
int MatchRules::is_squad_match()
{
    if (!net_session_slot()->in_net_game())
        return 0;
    int teams = unset(this);
    args_.fetch_int_value(kNumTeams, &teams, true);
    return teams > 0 ? 1 : 0;
}

// 0x00553630
int MatchRules::get_view_layout()
{
    uint32_t mode;
    args_.read_hash_value(kScreenMode, &mode, true);
    if (mode == kSplitScreen) {
        auto* prefs = modules::Skate::sole()->get_split_view_settings();
        uint32_t viewport;
        prefs->fetch_setting(kScreenLayoutKey)->read_hash_value(kChecksum, &viewport, true);
        if (viewport == kViewportVertical)
            return 1;
        return viewport == kViewportHorizontal ? 2 : 1;
    }
    if (mode == kSingle)
        return 0;
    return mode == kHorse ? 4 : 0;
}

// 0x005536D0
void MatchRules::increment_round()
{
    int round = 0;
    args_.fetch_int_value(kRound, &round, false);
    args_.put_int(kRound, round + 1);
}

// 0x00553C90
bool __cdecl cfunc_override_game_mode_options(script::Struct* params, script::Script*)
{
    return the_game_mode()->force_match_settings(params);
}

} // namespace goals
