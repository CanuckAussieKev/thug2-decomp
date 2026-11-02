// Skatetris, TrickBeat, TimedScoreRun and TrickObjects goals. See trick_goals.h.

#include "game/skate/trick_goals.h"

#include <cstdint>
#include <cstring>

#include "engine/core/checksums.h"
#include "engine/core/mem.h"
#include "frontend/menu_element.h"
#include "frontend/screen_element.h"
#include "frontend/screen_element_manager.h"
#include "frontend/text_element.h"
#include "engine/maths/random.h"
#include "engine/objects/entity.h"
#include "engine/objects/smtptr.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "game/skate/profile_manager.h"
#include "game/rider/rider.h"
#include "platform/game_clock.h"
#include "platform/stopwatch.h"
#include "game/goals/goal_manager.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module.h"
#include "game/goals/objective.h"
#include "engine/objects/control_pad.h"
#include "engine/gfx/tod_manager.h"
#include "game/goals/create_a_trick.h"
#include "game/rider/skater_score.h"
#include "game/rider/skater_state.h"
#include <gsl/pointers>
#include <array>
#include "platform/compiler.h"
#include "engine/core/checked_index.h"
#include "engine/core/down_cast.h"
#include "engine/core/format.h"

THUG2_MSVC_WARNING(disable : 4996) // (the original's strcpy / strcat into its fixed buffers, kept)

namespace goals {

namespace {
double g_trick_goals_three_quarters{0.75};
float g_trick_goals_beat_milliseconds{15000.0f};

// Names.
constexpr uint32_t kTrialRunFlag = 0x54D3CAC1;         // testing_goal
constexpr uint32_t kJustWonGoal = 0xC309CAD1;          // just_won_goal
constexpr uint32_t kTricksAdded = 0xC93DA683;          // (the single combo's tricks have been added)
constexpr uint32_t kCombo = 0x4EC3CFB5;                // combo
constexpr uint32_t kSingleCombo = 0xDF47B144;          // single_combo
constexpr uint32_t kTrickTris = 0xEA7BA666;            // TrickTris
constexpr uint32_t kTrickTime = 0xDED8540A;            // trick_time
constexpr uint32_t kCurrentTrickTime = 0x648D8D2A;     // (the current interval between tricks)
constexpr uint32_t kTricksDoneCount = 0x8D958F01;      // tricks_completed
constexpr uint32_t kNoAlphaCheck = 0x60B827D5;         // (0: the tricks aren't checked or faded this frame)
constexpr uint32_t kFadedAlpha = 0x855E6DA4;           // goal_tetris_faded_trick_alpha
constexpr uint32_t kUnfadedAlpha = 0xDBE5B19E;         // goal_tetris_unfaded_trick_alpha
constexpr uint32_t kExpire = 0xD34EB52B;               // expire
constexpr uint32_t kTetrisMenu = 0xE35EC715;           // tetris_tricks_menu
constexpr uint32_t kMaxTricksName = 0x89473DB7;        // max_tricks
constexpr uint32_t kId = 0x40C698AF;                   // id
constexpr uint32_t kResetContainer = 0x235BDE21;       // goal_tetris_reset_trick_container
constexpr uint32_t kRedTricks = 0xD02AC196;            // (the tricks are red)
constexpr uint32_t kTurnRedScript = 0xA39B24E0;        // (a script: a trick back from red)
constexpr uint32_t kDropTrickScript = 0x23A6A456;      // goal_tetris_remove_trick
constexpr uint32_t kAccelerationInterval = 0x01181B29; // acceleration_interval
constexpr uint32_t kAccelerationPercent = 0x076F65C8;  // acceleration_percent
constexpr uint32_t kTetrisKeyCombos = 0x7BE1E689;      // goal_tetris_key_combos
constexpr uint32_t kStackTrickList = 0xEB79FB49;       // goal_tetris_tricks
constexpr uint32_t kTrick = 0x270F56E1;                // trick
constexpr uint32_t kGrind = 0x255ED86F;                // Grind
constexpr uint32_t kText = 0xC4745838;                 // text
constexpr uint32_t kKeyCombo = 0xACFDB27A;             // key_combo
constexpr uint32_t kRandomSpin = 0x8C8ABD19;           // random_spin
constexpr uint32_t kSpin = 0xEDF5DB70;                 // spin
constexpr uint32_t kPerfect = 0x1C39F1B9;              // perfect
constexpr uint32_t kNumTaps = 0xA4BEE6A1;              // num_taps
constexpr uint32_t kTrickMapping = 0xD544AA2D;         // trick_mapping
constexpr uint32_t kParams = 0x7031F10C;               // params
constexpr uint32_t kExtraTricks = 0x6E855102;          // ExtraTricks
constexpr uint32_t kName = 0xA1DC81F9;                 // name
constexpr uint32_t kDims = 0x34A68574;                 // dims
constexpr uint32_t kFont = 0x2F6BF72D;                 // font
constexpr uint32_t kNewTrickFont = 0xBC19C379;         // newtrickfont
constexpr uint32_t kNotFocusable = 0x1D944426;         // not_focusable
constexpr uint32_t kSmall = 0x8ABA15EC;                // small
constexpr uint32_t kTrickTextDoubleTap = 0xE7ABFB95;   // goal_tetris_trick_text_double_tap
constexpr uint32_t kTrickText = 0x8856C817;            // goal_tetris_trick_text
constexpr uint32_t kNoKeyCombo = 0x7B716096;           // no_key_combo
constexpr uint32_t kPushTrickScript = 0x668CC9E3;      // goal_tetris_add_trick
constexpr uint32_t kAddRedTrick = 0x8E68AFC2;          // goal_tetris_add_red_trick
constexpr uint32_t kTurnTrickRed = 0xEB016D1E;         // goal_tetris_turn_trick_red
constexpr uint32_t kChainLength = 0x67DD90CA;          // combo_size
constexpr uint32_t kTrickRemovedSound = 0xE4EA743A;    // goal_tetris_play_trick_removed_sound
constexpr uint32_t kTrickAddDelay = 0x7824963C;        // wait_to_add_tricks
constexpr uint32_t kTimeToStop = 0xA6D96603;           // time_to_stop_adding_tricks
constexpr uint32_t kTrickTrisTotal = 0xBFD3A831;       // tricktris_total_to_win
constexpr uint32_t kStackBlockSize = 0x45A375C9;       // tricktris_block_size
// TrickBeat's.
constexpr uint32_t kBeatPattern = 0x407CCE70;        // beat_pattern
constexpr uint32_t kTempo = 0x8B4A1490;              // tempo
constexpr uint32_t kBeatWindowTime = 0x5CCDD89D;     // beat_window_time
constexpr uint32_t kTotalToWin = 0xD11D2F91;         // total_to_win
constexpr uint32_t kBeatFadedAlpha = 0x465128A1;     // goal_trick_beat_faded_trick_alpha
constexpr uint32_t kBeatUnfadedAlpha = 0xA02433CB;   // goal_trick_beat_unfaded_trick_alpha
constexpr uint32_t kWaitTime = 0xEB7BBCCA;           // wait_time
constexpr uint32_t kPulseTrick = 0x04CDC75C;         // goal_trick_beat_pulse_trick
constexpr uint32_t kBeatResetContainer = 0xE721EEFD; // goal_trick_beat_reset_trick_container
constexpr uint32_t kBeatRemoveTrick = 0x0118FB06;    // goal_trick_beat_remove_trick
constexpr uint32_t kSounds = 0x0EDCF90E;             // sounds
constexpr uint32_t kVol = 0xF6A36814;                // vol
constexpr uint32_t kPitch = 0xD8604126;              // pitch
constexpr uint32_t kPulse = 0xE4E93525;              // pulse
constexpr uint32_t kBlockSize = 0xCE5189EE;          // block_size
constexpr uint32_t kBeatKeyCombos = 0x548B4A38;      // goal_trick_beat_key_combos
constexpr uint32_t kBeatTricks = 0x09381808;         // goal_trick_beat_tricks
constexpr uint32_t kBeatMenu = 0xBF7DF8D6;           // trick_beat_tricks_menu
constexpr uint32_t kBeatAddTrick = 0x635CFFDF;       // goal_trick_beat_add_trick
// TrickObjects'.
constexpr uint32_t kRequiredScore = 0x1B35D319; // Required_Score
constexpr uint32_t kGoalObjects = 0x3CCE31F0;   // goal_objects
constexpr uint32_t kClusterId = 0x3C95CCD7;     // cluster_id
constexpr uint32_t kFlag = 0x2E0B1465;          // flag
constexpr uint32_t kSkaterState = 0x43686585;   // SkaterState (the component)
constexpr uint32_t kSkaterScore = 0xD262802F;   // SkaterScore (the component)

// The red-tricks threshold (0x0064E918): three quarters of max_tricks.
double three_quarters() { return g_trick_goals_three_quarters; }

// The C runtime's _ftol (0x006236FC): truncation, the low 32 bits of the 64-bit result.
int ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

uint32_t get_goal_id(Objective* goal) { return goal->get_objective_key(); }
// Objective's own Update / Activate (the types' overrides end with them).
bool base_update(Objective* goal) { return goal->Objective::tick_frame(); }
bool base_activate(Objective* goal) { return goal->Objective::activate(); }
// The goal manager (the skate module's, 0x005591B0) winning / losing / starting a goal.
void* objective_board() { return get_objective_board(); }
void complete_objective(void* manager, uint32_t id) { static_cast<GoalManager*>(manager)->complete_objective(id); }
void fail_objective(void* manager, uint32_t id) { static_cast<GoalManager*>(manager)->fail_objective(id); }

// The skate module (0x007CE478): the local skater, its score (modules::PointTally), the tricks the score has seen.
modules::Skate* get_skate() { return modules::g_skate_module.get(); }
objects::Rider* local_skater() { return modules::Skate::sole()->get_home_rider(); }
modules::PointTally* get_score_of(objects::Rider* skater) { return skater->get_points_total(); }
int total_score(modules::PointTally* score) { return score->get_total_score(); }
// The points of the trick in progress: the combo's base points times its multiplier (as a whole number).
int pending_points(modules::PointTally* score)
{
    auto* tally = score;
    return ftol(static_cast<double>(tally->pot_) * tally->multiplier_);
}
// How often the trick (by name / key combo) has been done with this spin, perfect, taps.
int get_trick_count(modules::PointTally* score, uint32_t trick, int spin, bool perfect, int taps)
{
    return score->count_trick_after_taps(trick, spin, perfect, taps);
}
int key_combo_count(modules::PointTally* score, uint32_t key_combo, int spin, bool perfect, int taps)
{
    return score->count_key_combo(key_combo, spin, perfect, taps);
}

// The screen elements: the manager (0x00701440), an element by id (its raw pointer: the smart pointer the lookup
// makes is let go at once, as in the original), the containers and text elements made for the tricks.
using frontend::ElementPtr;
void* ui_tree_handle() { return frontend::g_ui_element_hub.get(); }
frontend::UiTree* hub(void* manager) { return static_cast<frontend::UiTree*>(manager); }
frontend::UiWidget* element_by_id(void* manager, uint32_t id)
{
    ElementPtr element;
    hub(manager)->fetch_part(&element, id, 1);
    frontend::UiWidget* raw = element.get_pull();
    element.get_release();
    return raw;
}
void change_opacity(frontend::UiWidget* element, float alpha) { element->change_opacity(alpha, 1); }
gsl::owner<void*> new_container() { return new frontend::GroupWidget; }
gsl::owner<void*> new_text() { return new frontend::LabelWidget; }
void set_id(void* element, uint32_t id) { static_cast<objects::Entity*>(element)->change_serial(id); }
// The manager's +4 (adds an element) / an element's +4 (its properties from a structure).
void add_to_manager(void* manager, void* element)
{
    static_cast<frontend::UiTree*>(manager)->enrol_subject(static_cast<objects::Entity*>(element));
}
void change_traits(void* element, script::Struct* params)
{
    static_cast<objects::Entity*>(element)->change_traits(params);
}
// `child` under `parent` (0x004ABBE0, by smart pointers made for the call).
void change_owner(void* manager, void* parent, void* child)
{
    ElementPtr child_ptr;
    child_ptr.assign_value(static_cast<frontend::UiWidget*>(child));
    ElementPtr parent_ptr;
    parent_ptr.assign_value(static_cast<frontend::UiWidget*>(parent));
    hub(manager)->change_owner(&parent_ptr, &child_ptr, 1);
    parent_ptr.get_release();
    child_ptr.get_release();
}

script::Script* get_spawn(uint32_t name, script::Struct* params)
{
    return script::spawn_script(name, params, 0, nullptr, 0, 0, false, false, false, false);
}
void get_run(uint32_t name, script::Struct* params) { script::launch_script(name, params, nullptr, false, nullptr); }
// A spawned script run at once on `object`.
void run_on(script::Script* script, void* object)
{
    script->entity_.assign_value(static_cast<objects::Entity*>(object));
    script->tick_frame();
}

// "<prefix><name>": a grind's front and back side names.
uint32_t prefixed_checksum(const char* prefix, const char* name)
{
    std::array<char, 512> buffer{};
    std::strcpy(buffer.data(), prefix);
    std::strcat(buffer.data(), name);
    return checksums::hash_from_text(buffer.data());
}

} // namespace

// --- Skatetris ---------------------------------------------------------------------------------------------------

// 0x0056F090
FallingTricksGoal::FallingTricksGoal(script::Struct* params) : Objective(params) {}

// 0x0056F0B0
FallingTricksGoal::~FallingTricksGoal() = default;

// 0x0056F0C0
bool FallingTricksGoal::get_win()
{
    const bool testing = args_->has_bare_flag(kTrialRunFlag);
    args_->put_name_key(0u, kJustWonGoal);
    disengage(false, true); // Deactivate
    args_->remove_flag(kJustWonGoal);
    if (testing)
        args_->put_name_key(0u, kTrialRunFlag);
    const bool won = Objective::get_win();
    args_->remove_flag(kTrialRunFlag);
    return won;
}

// 0x0056F130
void FallingTricksGoal::clear_tricks()
{
    trick_total_ = 0;
    stopwatch_ = 0;
    for (int i = 0; i < kMaxTricks; ++i) {
        StackedTrickEntry& t = core::at(tricks_, i);
        t.free = false;
        t.trick = 0;
        t.trick2 = 0;
        t.key_combo_ = 0;
        t.spin = 0;
        t.taps = 1;
        t.perfect = false;
        core::at(group_used_, i) = false;
    }
    args_->remove_flag(kTricksAdded);
}

// 0x0056F180
bool FallingTricksGoal::all_free()
{
    for (const StackedTrickEntry& t : tricks_) {
        if (!t.free)
            return false;
    }
    return true;
}

namespace {
bool integer_or_flag(script::Struct* params, uint32_t name)
{
    int value = 0;
    params->fetch_int_value(name, &value, 0);
    return value || params->has_bare_flag(name);
}
} // namespace

// 0x0056F1A0
bool FallingTricksGoal::is_combo() { return integer_or_flag(args_.get(), kCombo); }
// 0x0056F1F0
bool FallingTricksGoal::is_single_combo() { return integer_or_flag(args_.get(), kSingleCombo); }
// 0x0056F240
bool FallingTricksGoal::is_trick_tris() { return integer_or_flag(args_.get(), kTrickTris); }

// 0x0056F2C0
bool FallingTricksGoal::disengage(bool unused, bool reset_tree)
{
    clear_tricks();
    return Objective::disengage(unused, reset_tree);
}

// 0x0056F2E0
void FallingTricksGoal::prepare_all()
{
    trick_total_ = 0;
    stopwatch_ = 0;
    int combo_time = 0;
    args_->fetch_int_value(kTrickTime, &combo_time, 1);
    args_->put_int(kCurrentTrickTime, combo_time);
    args_->put_int(kTricksDoneCount, 0);
    // The first trick comes 200 ms in (at once for a single combo).
    stopwatch_ = integer_or_flag(args_.get(), kSingleCombo) ? 0 : combo_time - 200;
    for (int i = 0; i < kMaxTricks; ++i) {
        StackedTrickEntry& t = core::at(tricks_, i);
        t.free = true;
        t.trick = 0;
        t.trick2 = 0;
        t.key_combo_ = 0;
        t.spin = 0;
        t.taps = 1;
        t.perfect = false;
        core::at(group_used_, i) = false;
    }
}

// 0x0056F3B0
void FallingTricksGoal::update_alphas()
{
    int check = 0; // (only read when given)
    const bool check_given = args_->fetch_int_value(kNoAlphaCheck, &check, 0);
    modules::PointTally* score = get_score_of(local_skater());
    void* manager = ui_tree_handle();
    const float faded = script::fetch_float(kFadedAlpha);
    const float unfaded = script::fetch_float(kUnfadedAlpha);
    for (const StackedTrickEntry& t : tricks_) {
        if (t.free)
            continue;
        frontend::UiWidget* element = element_by_id(manager, t.element);
        bool done = false;
        if (!check_given || check) {
            if (t.key_combo_)
                done = key_combo_count(score, t.key_combo_, t.spin, t.perfect, t.taps) > 0;
            else if (t.trick)
                done = get_trick_count(score, t.trick, t.spin, t.perfect, 1) > 0 ||
                       (t.trick2 && get_trick_count(score, t.trick2, t.spin, t.perfect, 1) > 0);
        }
        change_opacity(element, done ? faded : unfaded);
    }
}

// 0x0056F500
void FallingTricksGoal::expire()
{
    if (integer_or_flag(args_.get(), kSingleCombo) || is_trick_tris()) {
        lose();
        return;
    }
    if (all_free()) {
        void* manager = objective_board();
        complete_objective(manager, get_goal_id(this));
        return;
    }
    invoke_hook_script(kExpire);
    if (run_ending_ != 2)
        disengage(false, true); // Deactivate
}

// 0x0056F5B0
void FallingTricksGoal::clear_tricks_and_menu()
{
    trick_total_ = 0;
    stopwatch_ = 0;
    for (int i = 0; i < kMaxTricks; ++i) {
        StackedTrickEntry& t = core::at(tricks_, i);
        t.free = true;
        t.trick = 0;
        t.trick2 = 0;
        t.key_combo_ = 0;
        core::at(group_used_, i) = false;
    }
    args_->remove_flag(kTricksAdded);
    hub(ui_tree_handle())->dismantle_part(kTetrisMenu, 1, 1, nullptr);
}

// 0x0056F610
bool FallingTricksGoal::activate()
{
    if (is_operative())
        return false;
    prepare_all();
    return base_activate(this);
}

// 0x0056F630
void FallingTricksGoal::drop_move_entry(int index)
{
    int max_tricks = 0;
    args_->fetch_int_value(kMaxTricksName, &max_tricks, 1);
    const uint32_t id = core::at(tricks_, index).element;
    void* manager = ui_tree_handle();
    ElementPtr element;
    hub(manager)->fetch_part(&element, id, 1);
    script::StructPtr params = script::make_struct();
    params->put_name_key(kId, id);
    get_run(kResetContainer, params.get());
    params.reset();

    // Red tricks back to normal once the stack is under three quarters full.
    int red = 0;
    args_->fetch_int_value(kRedTricks, &red, 0);
    if (red == 1 && static_cast<double>(trick_total_ - 1) < static_cast<double>(max_tricks) * three_quarters()) {
        args_->put_int(kRedTricks, 0);
        for (const StackedTrickEntry& t : tricks_) {
            if (t.free)
                continue;
            frontend::UiWidget* trick = element_by_id(manager, t.element);
            run_on(get_spawn(kTurnRedScript, nullptr), trick);
        }
    }

    script::StructPtr remove_params = script::make_struct();
    if (is_trick_tris())
        remove_params->put_name_key(0u, kTrickTris);
    run_on(get_spawn(kDropTrickScript, remove_params.get()), element.get_pull());
    remove_params.reset();

    // Every acceleration_interval tricks, the interval between tricks shortens by acceleration_percent.
    int completed = 0;
    args_->fetch_int_value(kTricksDoneCount, &completed, 0);
    ++completed;
    int interval = 0;
    args_->fetch_int_value(kAccelerationInterval, &interval, 0);
    if (interval && completed % interval == 0) {
        float percent = 0.0f;
        args_->fetch_float(kAccelerationPercent, &percent, 0);
        int time = 0;
        args_->fetch_int_value(kCurrentTrickTime, &time, 1);
        time = ftol((1.0 - percent) * time);
        args_->put_int(kCurrentTrickTime, time);
    }
    args_->put_int(kTricksDoneCount, completed);
    --trick_total_;
    core::at(tricks_, index).free = true;
}

// 0x0056FA30
void FallingTricksGoal::add_tricks(int count)
{
    int max_tricks = 0;
    args_->fetch_int_value(kMaxTricksName, &max_tricks, 1);
    const int total = trick_total_ + count;
    const bool combo = integer_or_flag(args_.get(), kCombo) || is_single_combo();
    if (total > max_tricks) {
        const uint32_t id = get_goal_id(this);
        fail_objective(objective_board(), id);
        return;
    }
    int group = 0;
    if (combo) {
        for (int i = 0; i < kMaxTricks; ++i) {
            if (!core::at(group_used_, i)) {
                core::at(group_used_, i) = true;
                group = i;
                break;
            }
        }
    }
    for (int remaining = count; remaining > 0; --remaining) {
        void* manager = ui_tree_handle();
        script::Array* key_combos = nullptr;
        args_->fetch_list(kTetrisKeyCombos, &key_combos, 0);
        uint32_t key_combo = 0;
        uint32_t trick_name = 0;
        int slot = 0;
        for (int i = 0; i < kMaxTricks; ++i) {
            if (core::at(tricks_, i).free) {
                StackedTrickEntry& t = core::at(tricks_, i);
                slot = i;
                t.free = false;
                t.group = group;
                t.key_combo_ = 0;
                t.trick = 0;
                t.trick2 = 0;
                t.spin = 0;
                t.taps = 1;
                t.perfect = false;
                break;
            }
        }
        StackedTrickEntry& t = core::at(tricks_, slot);
        const char* key_combo_text = nullptr;
        int taps = 1;
        std::array<char, 512> text{};
        if (!key_combos) {
            // A random trick of goal_tetris_tricks.
            script::Array* tricks = nullptr;
            args_->fetch_list(kStackTrickList, &tricks, 1);
            auto* trick =
                tricks->struct_at(maths::rnd(static_cast<int>(tricks->extent_)));
            const char* name = nullptr;
            trick->fetch_text(kTrick, &name, 1);
            if (trick->has_bare_flag(kGrind)) {
                t.trick = prefixed_checksum("FS ", name);
                t.trick2 = prefixed_checksum("BS ", name);
            } else {
                t.trick = checksums::hash_from_text(name);
            }
            const char* trick_text = nullptr;
            trick->fetch_text(kText, &trick_text, 1);
            std::strcpy(text.data(), trick_text);
            if (!trick->read_hash_value(kKeyCombo, &key_combo, 0))
                trick->fetch_text(kKeyCombo, &key_combo_text, 0);
        } else {
            // A key combo: its trick from the skater's trick mapping.
            const int index = pick_key_combo(key_combos);
            if (index == -1)
                return;
            if (key_combos->kind_tag_ == script::kQbvalName) {
                key_combo = key_combos->fetch_part(static_cast<uint32_t>(index));
            } else {
                auto* entry = key_combos->struct_at(static_cast<uint32_t>(index));
                entry->read_hash_value(kKeyCombo, &key_combo, 1);
                int spin = 0;
                if (entry->fetch_int_value(kRandomSpin, &spin, 0)) {
                    if (spin < 360)
                        spin = 360;
                    t.spin = maths::rnd(spin / 180) + 1;
                    if (t.spin == 1)
                        t.spin = 0;
                }
                if (entry->fetch_int_value(kSpin, &spin, 0))
                    t.spin = spin / 180;
                if (entry->has_bare_flag(kPerfect))
                    t.perfect = true;
                if (entry->fetch_int_value(kNumTaps, &taps, 0))
                    t.taps = taps;
            }
            void* profile = modules::Skate::sole()->fetch_account(static_cast<script::Struct*>(nullptr));
            auto* mapping = static_cast<objects::RiderIdentity*>(profile)->fetch_details_params(kTrickMapping);
            const char* trick_text = nullptr;
            if (!mapping->read_hash_value(key_combo, &trick_name, 0)) {
                // A special trick slot: the skater's trick in it.
                int special_slot = 0;
                mapping->fetch_int_value(key_combo, &special_slot, 1);
                objects::Rider* skater = local_skater();
                if (skater) {
                    core::at(skater->trick_slots_, special_slot)->other_params_->fetch_text(
                        kName, &trick_text, 1);
                    t.taps = 1;
                }
            }
            if (!trick_text) {
                script::Struct* trick = script::read_sub_struct(trick_name);
                if (!trick)
                    trick = script::fetch_list(trick_name)->struct_at(0);
                script::Struct* trick_params = nullptr;
                trick->read_sub_struct(kParams, &trick_params, 1);
                // A double tap: the trick's ExtraTricks' (if it has none, a single tap after all).
                while (taps > 1) {
                    --taps;
                    uint32_t extra = 0;
                    if (!trick_params->read_hash_value(kExtraTricks, &extra, 0)) {
                        taps = 1;
                        t.taps = 1;
                        break;
                    }
                    auto* extra_trick = script::fetch_list(extra)->struct_at(0);
                    extra_trick->read_sub_struct(kParams, &trick_params, 1);
                }
                trick_params->fetch_text(kName, &trick_text, true);
            }
            core::at(text, 0) = '\0';
            if (t.spin) {
                THUG2_MSVC_WARNING(suppress : 4996)
                if (t.perfect)
                    text::format_to(text, "Perfect ");
                // (written over "Perfect ": the original's too)
                THUG2_MSVC_WARNING(suppress : 4996)
                text::format_to(text, "\\c5%i\\c0 ", t.spin * 180);
            }
            std::strcat(text.data(), trick_text);
            t.key_combo_ = key_combo;
        }

        // The trick's container in the stack, its text, and its key combo's text.
        gsl::owner<script::Struct*> params = script::new_struct();
        gsl::owner<void*> container = new_container();
        set_id(container, objects::kUnassignedId);
        add_to_manager(manager, container);
        const int height = combo && remaining == 1 ? 40 : 20;
        params->insert_pair(kDims, 100.0f, static_cast<float>(height));
        change_traits(container, params);
        frontend::UiWidget* menu = element_by_id(manager, kTetrisMenu);
        menu->set_focus_sent(0);
        change_owner(manager, menu, container);
        script::free_struct(params);

        params = script::new_struct();
        params->append_word(kText, text.data());
        params->put_name_key(kFont, kNewTrickFont);
        params->put_name_key(0u, kNotFocusable);
        gsl::owner<void*> text_element = new_text();
        set_id(text_element, objects::kUnassignedId);
        add_to_manager(manager, text_element);
        change_traits(text_element, params);
        change_owner(manager, container, text_element);
        script::free_struct(params);

        if (key_combo_text || key_combo) {
            script::Struct* texts = script::read_sub_struct(t.taps == 2 ? kTrickTextDoubleTap : kTrickText);
            if (!key_combo_text)
                texts->fetch_text(key_combo, &key_combo_text, 0);
            params = script::new_struct();
            params->append_word(kText, key_combo_text);
            params->put_name_key(kFont, kSmall);
            params->put_name_key(0u, kNotFocusable);
            gsl::owner<void*> key_text = new_text();
            set_id(key_text, objects::kUnassignedId);
            add_to_manager(manager, key_text);
            change_traits(key_text, params);
            change_owner(manager, container, key_text);
            script::free_struct(params);
        }
        t.element = static_cast<objects::Entity*>(container)->ident_; // its id
        ++trick_total_;

        // The add script (red once the stack is three quarters full); then the stack's tricks turned red.
        params = script::new_struct();
        if (!key_combo_text)
            params->put_name_key(0u, kNoKeyCombo);
        auto stack_full = [&]() {
            return !integer_or_flag(args_.get(), kTrickTris) && !integer_or_flag(args_.get(), kSingleCombo) &&
                   static_cast<double>(max_tricks) * three_quarters() < static_cast<double>(trick_total_);
        };
        const uint32_t add_script = stack_full() ? kAddRedTrick : kPushTrickScript;
        run_on(get_spawn(add_script, params), container);
        script::free_struct(params);
        if (stack_full()) {
            args_->put_int(kRedTricks, 1);
            for (const StackedTrickEntry& other : tricks_) {
                if (other.free)
                    continue;
                frontend::UiWidget* element = element_by_id(manager, other.element);
                run_on(get_spawn(kTurnTrickRed, nullptr), element);
            }
        }
    }
}

// 0x00570A70
void FallingTricksGoal::check_combos()
{
    modules::PointTally* score = get_score_of(local_skater());
    int combo_size = 0;
    args_->fetch_int_value(kChainLength, &combo_size, 1);
    for (int group = 0; group < kMaxTricks; ++group) {
        if (!core::at(group_used_, group))
            continue;
        bool sound_played = false;
        int done = 0;
        for (const StackedTrickEntry& t : tricks_) {
            const bool in_group = !t.free && t.group == group;
            if (t.key_combo_) {
                if (in_group && key_combo_count(score, t.key_combo_, t.spin, t.perfect, t.taps) > 0)
                    ++done;
            } else if (t.trick) {
                if (in_group && get_trick_count(score, t.trick, t.spin, t.perfect, t.taps) > 0)
                    // NOLINTNEXTLINE(bugprone-branch-clone): the original's, as written
                    ++done;
                else if (t.trick2 && in_group && get_trick_count(score, t.trick2, t.spin, t.perfect, t.taps) > 0)
                    ++done;
            }
        }
        if (done < combo_size)
            continue;
        for (int i = 0; i < kMaxTricks; ++i) {
            if (!core::at(tricks_, i).free && core::at(tricks_, i).group == group) {
                if (!sound_played)
                    get_run(kTrickRemovedSound, nullptr);
                sound_played = true;
                drop_move_entry(i);
            }
        }
        core::at(group_used_, group) = false;
        if (integer_or_flag(args_.get(), kSingleCombo)) {
            void* manager = objective_board();
            complete_objective(manager, get_goal_id(this));
        }
    }
}

// 0x00570C40
bool FallingTricksGoal::tick_frame()
{
    int wait = 0;
    args_->fetch_int_value(kTrickAddDelay, &wait, 0);
    if (wait)
        return base_update(this);
    int stop_time = 1;
    if (!is_single_combo() && args_->fetch_int_value(kTimeToStop, &stop_time, 0))
        stop_time *= 1000;
    auto win = [&]() {
        void* manager = objective_board();
        complete_objective(manager, get_goal_id(this));
        return true;
    };
    if (is_trick_tris()) {
        if (is_operative() && !is_frozen()) {
            int completed = 0;
            args_->fetch_int_value(kTricksDoneCount, &completed, 0);
            int total = 0;
            args_->fetch_int_value(kTrickTrisTotal, &total, 1);
            if (completed >= total)
                return win();
        }
    } else if (is_operative() && seconds_remaining_ < stop_time && !is_frozen() && all_free()) {
        return win();
    }
    if (is_operative() && !is_frozen())
        update_alphas();
    if (is_trick_tris()) {
        if (!(cur_phase_ & kStateNoTimeLimit) && !(static_cast<double>(seconds_remaining_) > 0.0))
            return base_update(this);
        if (is_frozen())
            return base_update(this);
        int completed = 0;
        args_->fetch_int_value(kTricksDoneCount, &completed, 0);
        int total = 0;
        args_->fetch_int_value(kTrickTrisTotal, &total, 1);
        int used = 0;
        for (const StackedTrickEntry& t : tricks_) {
            if (!t.free)
                ++used;
        }
        int block_size = 0;
        args_->fetch_int_value(kStackBlockSize, &block_size, 1);
        if (used >= block_size || used + completed >= total)
            return base_update(this);
        int count = total - used - completed;
        if (count > block_size - used)
            count = block_size - used;
        add_tricks(count);
        return base_update(this);
    }
    if (!is_operative() || (!(cur_phase_ & kStateNoTimeLimit) && seconds_remaining_ < stop_time) || is_frozen())
        return base_update(this);
    stopwatch_ += ftol(frame_clock::get_slomo_frame_seconds() * 1000.0f);
    if (is_single_combo()) {
        if (args_->has_bare_flag(kTricksAdded) || stopwatch_ <= 50)
            return base_update(this);
        int combo_size = 0;
        args_->fetch_int_value(kChainLength, &combo_size, 1);
        add_tricks(combo_size);
        args_->put_name_key(0u, kTricksAdded);
        return base_update(this);
    }
    int combo_time = 0;
    args_->fetch_int_value(kCurrentTrickTime, &combo_time, 1);
    if (stopwatch_ <= combo_time)
        return base_update(this);
    stopwatch_ = 0;
    int count = 1;
    if (is_combo())
        args_->fetch_int_value(kChainLength, &count, 1);
    add_tricks(count);
    return base_update(this);
}

// 0x00570F60
void FallingTricksGoal::check_tricks()
{
    int check = 0;
    if (args_->fetch_int_value(kNoAlphaCheck, &check, 0)) {
        if (!check)
            return;
        args_->put_int(kNoAlphaCheck, 0);
    }
    const bool combo = integer_or_flag(args_.get(), kSingleCombo) || is_combo();
    modules::PointTally* score = get_score_of(local_skater());
    if (combo) {
        check_combos();
        return;
    }
    bool sound_played = false;
    for (int i = 0; i < kMaxTricks; ++i) {
        const StackedTrickEntry& t = core::at(tricks_, i);
        if (t.free)
            continue;
        bool done = false;
        if (t.trick)
            done = get_trick_count(score, t.trick, t.spin, t.perfect, 1) != 0 ||
                   (t.trick2 && get_trick_count(score, t.trick2, t.spin, t.perfect, 1) != 0);
        else
            done = t.key_combo_ && key_combo_count(score, t.key_combo_, t.spin, t.perfect, t.taps) != 0;
        if (!done)
            continue;
        if (!sound_played)
            get_run(kTrickRemovedSound, nullptr);
        sound_played = true;
        drop_move_entry(i);
    }
}

// --- TimedScoreRun -------------------------------------------------------------------------------------------------

// 0x005710B0
TimedScoreRunGoal::TimedScoreRunGoal(script::Struct* params) : Objective(params) {}

// 0x005710D0
TimedScoreRunGoal::~TimedScoreRunGoal() = default;

// 0x005710E0
bool TimedScoreRunGoal::shows_clock() { return is_operative() && !is_frozen(); }

// --- TrickBeat -----------------------------------------------------------------------------------------------------

// 0x00574960
void TrickBeatGoal::set_beat_pattern(script::Array* pattern, int tempo)
{
    script::duplicate_array(pattern_, pattern);
    beat_length_ = static_cast<float>(g_trick_goals_beat_milliseconds / static_cast<double>(tempo)); // 15000 / tempo
}

// 0x00574990
void TrickBeatGoal::clear_tricks()
{
    trick_total_ = 0;
    beat_time_ = 0;
    for (int i = 0; i < kMaxTricks; ++i) {
        BeatTrick& t = core::at(tricks_, i);
        t.free = true; // (overwritten below: the original stores both)
        t.trick = 0;
        t.trick2 = 0;
        t.landed = false;
        t.key_combo_ = 0;
        t.spin = 0;
        t.taps = 1;
        t.perfect = false;
        t.free = false;
        core::at(unknown45c_, i) = false;
    }
    args_->remove_flag(kTricksAdded);
}

// 0x005749F0
bool TrickBeatGoal::disengage(bool unused, bool reset_tree)
{
    clear_tricks();
    return Objective::disengage(unused, reset_tree);
}

namespace {
int game_clock() { return frame_clock::game_clock_elapsed() + objects::g_clock_offset; }
} // namespace

// 0x00574A10
void TrickBeatGoal::prepare_all()
{
    last_time_ = game_clock();
    trick_total_ = 0;
    beat_time_ = 0;
    completed_ = 0;
    args_->fetch_int_value(kBeatWindowTime, &window_time_, 1);
    args_->fetch_int_value(kTotalToWin, &total_to_win_, 1);
    args_->put_int(kTricksDoneCount, 0);
    for (int i = 0; i < kMaxTricks; ++i) {
        BeatTrick& t = core::at(tricks_, i);
        t.free = true;
        t.trick = 0;
        t.trick2 = 0;
        t.landed = false;
        t.key_combo_ = 0;
        t.spin = 0;
        t.taps = 1;
        t.perfect = false;
        core::at(unknown45c_, i) = false;
    }
}

// 0x00574AB0
void TrickBeatGoal::trick_landed(script::Struct* trick)
{
    script::Struct* params = trick;
    trick->read_sub_struct(kParams, &params, 1);
    const char* name = nullptr;
    params->fetch_text(kName, &name, 1);
    const uint32_t checksum = checksums::hash_from_text(name);
    if (!in_window_)
        return;
    void* manager = ui_tree_handle();
    const float alpha = script::fetch_float(kBeatFadedAlpha);
    for (BeatTrick& t : tricks_) {
        if (t.free || t.landed || t.trick != checksum)
            continue;
        t.landed = true;
        change_opacity(element_by_id(manager, t.element), alpha);
    }
}

// 0x00574B90
void TrickBeatGoal::clear_landed()
{
    void* manager = ui_tree_handle();
    const float alpha = script::fetch_float(kBeatUnfadedAlpha);
    for (BeatTrick& t : tricks_) {
        if (t.free || !t.landed)
            continue;
        t.landed = false;
        change_opacity(element_by_id(manager, t.element), alpha);
    }
}

// 0x00574C20
void TrickBeatGoal::reset_clock() { last_time_ = game_clock(); }

// 0x00574C40
TrickBeatGoal::TrickBeatGoal(script::Struct* params) : Objective(params)
{
    gsl::owner<script::Array*> memory = script::alloc_script_array();
    pattern_ = memory ? memory::construct_in<script::Array>(memory) : nullptr;
    pattern_->configure_size_kind(kBeats, script::kQbvalStruct);
    script::Array* pattern = nullptr;
    params->fetch_list(kBeatPattern, &pattern, 1);
    int tempo = 0;
    params->fetch_int_value(kTempo, &tempo, 1);
    script::duplicate_array(pattern_, pattern);
    beat_length_ = static_cast<float>(g_trick_goals_beat_milliseconds / static_cast<double>(tempo));
}

// 0x00574D50
bool TrickBeatGoal::activate()
{
    in_window_ = false;
    if (is_operative())
        return false;
    prepare_all();
    return base_activate(this);
}

// 0x00574D80
void TrickBeatGoal::pulse_tricks()
{
    for (const BeatTrick& t : tricks_) {
        if (t.free || t.landed)
            continue;
        frontend::UiWidget* element = element_by_id(ui_tree_handle(), t.element);
        script::StructPtr params = script::make_struct();
        auto window = static_cast<float>(window_time_);
        params->put_int(kWaitTime, ftol(window / static_cast<double>(frame_clock::get_time_scale())));
        script::Script* script = get_spawn(kPulseTrick, params.get());
        params.reset();
        run_on(script, element);
    }
}

// 0x00574F00
void TrickBeatGoal::drop_move_entry(int index)
{
    const uint32_t id = core::at(tricks_, index).element;
    ElementPtr element;
    hub(ui_tree_handle())->fetch_part(&element, id, 1);
    script::StructPtr params = script::make_struct();
    params->put_name_key(kId, id);
    get_run(kBeatResetContainer, params.get());
    params.reset();
    int red = 0;
    args_->fetch_int_value(kRedTricks, &red, 0); // (unused)
    script::StructPtr remove_params = script::make_struct();
    remove_params->put_name_key(0u, kTrickTris);
    run_on(get_spawn(kBeatRemoveTrick, remove_params.get()), element.get_pull());
    remove_params.reset();
    int completed = 0;
    args_->fetch_int_value(kTricksDoneCount, &completed, 0);
    ++completed;
    int interval = 0;
    args_->fetch_int_value(kAccelerationInterval, &interval, 0); // (unused)
    args_->put_int(kTricksDoneCount, completed);
    --trick_total_;
    core::at(tricks_, index).free = true;
}

// 0x00575140
TrickBeatGoal::~TrickBeatGoal()
{
    script::release_array_storage(pattern_);
    pattern_->get_clear();
    if (pattern_) {
        pattern_->get_clear();
        script::delete_array(pattern_);
    }
}

// 0x005751C0
void TrickBeatGoal::update_beat()
{
    const float slomo = frame_clock::get_time_scale();
    const int last = last_time_;
    const int elapsed = frame_clock::game_clock_elapsed() + (objects::g_clock_offset - last);
    beat_time_ += elapsed;
    last_time_ += elapsed;
    auto time = static_cast<double>(beat_time_);
    if (beat_time_ < 0)
        time += gfx::g_two_to32; // (unsigned)
    const double inverse = 1.0 / slomo;
    auto scale = static_cast<float>(inverse); // (kept as a float; the beat's length uses the unrounded value)
    auto beat = static_cast<float>(inverse * beat_length_);
    if (!(time <= beat)) {
        // A beat: its sounds, then on to the next.
        auto* step = pattern_->struct_at(static_cast<uint32_t>(beat_));
        script::Array* sounds = nullptr;
        if (step->fetch_list(kSounds, &sounds, 0)) {
            const int count = static_cast<int>(sounds->extent_);
            for (int i = 0; i < count; ++i) {
                auto* sound = sounds->struct_at(static_cast<uint32_t>(i));
                uint32_t name = 0;
                sound->read_hash_value(0u, &name, 1);
                float volume = 100.0f;
                float pitch = 100.0f;
                sound->fetch_float(kVol, &volume, 0);
                sound->fetch_float(kPitch, &pitch, 0);
                pitch = static_cast<float>(static_cast<double>(pitch) * slomo);
                sound::VolumeRef level;
                level.change_muted();
                core::at(level.lanes_, 0) = volume;
                core::at(level.lanes_, 1) = volume;
                sound::g_audio_mixer->emit_sound(name, &level, pitch, 0, nullptr, 0);
            }
        }
        if (++beat_ > kBeats - 1) {
            beat_ = 0;
            beat_time_ = 0;
        } else {
            beat_time_ -= ftol(static_cast<double>(scale) * beat_length_);
        }
        step = pattern_->struct_at(static_cast<uint32_t>(beat_));
        pulse_ = step->has_bare_flag(kPulse);
        return;
    }
    // The beat window: half of beat_window_time either side of the beat.
    const int half = window_time_ / 2;
    if (pulse_) {
        if (!(time <= static_cast<double>(beat) - static_cast<double>(half) * slomo)) {
            pulse_tricks();
            in_window_ = true;
        }
    } else if (!(time <= static_cast<double>(half) * slomo)) {
        in_window_ = false;
    }
}

// 0x00575450
void TrickBeatGoal::add_tricks(int count)
{
    for (int remaining = count; remaining > 0; --remaining) {
        void* manager = ui_tree_handle();
        script::Array* key_combos = nullptr;
        args_->fetch_list(kBeatKeyCombos, &key_combos, 0);
        uint32_t key_combo = 0;
        uint32_t trick_name = 0;
        int slot = 0;
        for (int i = 0; i < kMaxTricks; ++i) {
            if (core::at(tricks_, i).free) {
                BeatTrick& t = core::at(tricks_, i);
                slot = i;
                t.free = true;
                t.trick = 0;
                t.trick2 = 0;
                t.landed = false;
                t.key_combo_ = 0;
                t.spin = 0;
                t.taps = 1;
                t.perfect = false;
                t.free = false;
                t.unknown14 = 0;
                break;
            }
        }
        BeatTrick& t = core::at(tricks_, slot);
        const char* key_combo_text = nullptr;
        int taps = 1;
        std::array<char, 512> text{};
        if (!key_combos) {
            script::Array* tricks = nullptr;
            args_->fetch_list(kBeatTricks, &tricks, 1);
            auto* trick =
                tricks->struct_at(maths::rnd(static_cast<int>(tricks->extent_)));
            const char* name = nullptr;
            trick->fetch_text(kTrick, &name, 1);
            if (trick->has_bare_flag(kGrind)) {
                t.trick = prefixed_checksum("FS ", name);
                t.trick2 = prefixed_checksum("BS ", name);
            } else {
                t.trick = checksums::hash_from_text(name);
            }
            const char* trick_text = nullptr;
            trick->fetch_text(kText, &trick_text, 1);
            std::strcpy(text.data(), trick_text);
            if (!trick->read_hash_value(kKeyCombo, &key_combo, 0))
                trick->fetch_text(kKeyCombo, &key_combo_text, 0);
        } else {
            const int index = pick_key_combo(key_combos);
            if (index == -1)
                return;
            if (key_combos->kind_tag_ == script::kQbvalName) {
                key_combo = key_combos->fetch_part(static_cast<uint32_t>(index));
            } else {
                auto* entry = key_combos->struct_at(static_cast<uint32_t>(index));
                entry->read_hash_value(kKeyCombo, &key_combo, 1);
                int spin = 0;
                if (entry->fetch_int_value(kRandomSpin, &spin, 0)) {
                    if (spin < 360)
                        spin = 360;
                    t.spin = maths::rnd(spin / 180) + 1;
                    if (t.spin == 1)
                        t.spin = 0;
                }
                if (entry->fetch_int_value(kSpin, &spin, 0))
                    t.spin = spin / 180;
                if (entry->has_bare_flag(kPerfect))
                    t.perfect = true;
                if (entry->fetch_int_value(kNumTaps, &taps, 0))
                    t.taps = taps;
            }
            void* profile = modules::Skate::sole()->fetch_account(static_cast<script::Struct*>(nullptr));
            auto* mapping = static_cast<objects::RiderIdentity*>(profile)->fetch_details_params(kTrickMapping);
            const char* trick_text = nullptr;
            if (!mapping->read_hash_value(key_combo, &trick_name, 0)) {
                int special_slot = 0;
                mapping->fetch_int_value(key_combo, &special_slot, 1);
                objects::Rider* skater = local_skater();
                if (skater) {
                    core::at(skater->trick_slots_, special_slot)->other_params_->fetch_text(
                        kName, &trick_text, 1);
                    t.taps = 1;
                }
            }
            if (!trick_text) {
                script::Struct* trick = script::read_sub_struct(trick_name);
                if (!trick)
                    trick = script::fetch_list(trick_name)->struct_at(0);
                script::Struct* trick_params = nullptr;
                trick->read_sub_struct(kParams, &trick_params, 1);
                while (taps > 1) {
                    --taps;
                    uint32_t extra = 0;
                    if (!trick_params->read_hash_value(kExtraTricks, &extra, 0)) {
                        taps = 1;
                        t.taps = 1;
                        break;
                    }
                    auto* extra_trick = script::fetch_list(extra)->struct_at(0);
                    extra_trick->read_sub_struct(kParams, &trick_params, 1);
                }
                trick_params->fetch_text(kName, &trick_text, true);
            }
            core::at(text, 0) = '\0';
            if (t.spin) {
                THUG2_MSVC_WARNING(suppress : 4996)
                if (t.perfect)
                    text::format_to(text, "Perfect ");
                THUG2_MSVC_WARNING(suppress : 4996)
                text::format_to(text, "\\c5%i\\c0 ", t.spin * 180); // (over "Perfect ", as in the original)
            }
            std::strcat(text.data(), trick_text);
            t.key_combo_ = key_combo;
            t.trick = checksums::hash_from_text(trick_text); // (trick_landed matches by the trick's name)
        }

        gsl::owner<script::Struct*> params = script::new_struct();
        gsl::owner<void*> container = new_container();
        set_id(container, objects::kUnassignedId);
        add_to_manager(manager, container);
        params->insert_pair(kDims, 100.0f, 20.0f);
        change_traits(container, params);
        frontend::UiWidget* menu = element_by_id(manager, kBeatMenu);
        menu->set_focus_sent(0);
        change_owner(manager, menu, container);
        script::free_struct(params);

        params = script::new_struct();
        params->append_word(kText, text.data());
        params->put_name_key(kFont, kNewTrickFont);
        params->put_name_key(0u, kNotFocusable);
        gsl::owner<void*> text_element = new_text();
        set_id(text_element, objects::kUnassignedId);
        add_to_manager(manager, text_element);
        change_traits(text_element, params);
        change_owner(manager, container, text_element);
        script::free_struct(params);

        if (key_combo_text || key_combo) {
            script::Struct* texts = script::read_sub_struct(t.taps == 2 ? kTrickTextDoubleTap : kTrickText);
            if (!key_combo_text)
                texts->fetch_text(key_combo, &key_combo_text, 0);
            params = script::new_struct();
            params->append_word(kText, key_combo_text);
            params->put_name_key(kFont, kSmall);
            params->put_name_key(0u, kNotFocusable);
            gsl::owner<void*> key_text = new_text();
            set_id(key_text, objects::kUnassignedId);
            add_to_manager(manager, key_text);
            change_traits(key_text, params);
            change_owner(manager, container, key_text);
            script::free_struct(params);
        }
        t.element = static_cast<objects::Entity*>(container)->ident_;
        ++trick_total_;
        params = script::new_struct();
        if (!key_combo_text)
            params->put_name_key(0u, kNoKeyCombo);
        run_on(get_spawn(kBeatAddTrick, params), container);
        script::free_struct(params);
    }
}

// 0x005761D0
void TrickBeatGoal::remove_landed_tricks()
{
    for (int i = 0; i < kMaxTricks; ++i) {
        if (!core::at(tricks_, i).free && core::at(tricks_, i).landed) {
            ++completed_;
            drop_move_entry(i);
        }
    }
}

// 0x00576240
bool TrickBeatGoal::tick_frame()
{
    int wait = 0;
    args_->fetch_int_value(kTrickAddDelay, &wait, 0);
    if (wait)
        return base_update(this);
    if (is_operative() && !is_frozen() && completed_ >= total_to_win_) {
        void* manager = objective_board();
        complete_objective(manager, get_goal_id(this));
        return true;
    }
    if (is_operative())
        is_frozen(); // (the result isn't used)
    if (is_operative() && ((cur_phase_ & kStateNoTimeLimit) || static_cast<double>(seconds_remaining_) > 0.0) &&
        !is_frozen()) {
        update_beat();
        int used = 0;
        for (const BeatTrick& t : tricks_) {
            if (!t.free)
                ++used;
        }
        int block_size = 0;
        args_->fetch_int_value(kBlockSize, &block_size, 1);
        if (used < block_size && completed_ + used < total_to_win_) {
            int count = total_to_win_ - completed_ - used;
            if (count > block_size - used)
                count = block_size - used;
            add_tricks(count);
        }
    }
    return base_update(this);
}

// --- TrickObjects --------------------------------------------------------------------------------------------------

// 0x005763A0
TrickObjectsGoal::TrickObjectsGoal(script::Struct* params) : Objective(params) {}

// 0x005763C0
TrickObjectsGoal::~TrickObjectsGoal() = default;

// 0x00576400
void TrickObjectsGoal::evaluate_points()
{
    void* manager = objective_board();
    if (!has_every_bit()) {
        fail_objective(manager, get_goal_id(this));
        return;
    }
    int required = 0;
    if (!args_->fetch_int_value(kRequiredScore, &required, 0)) {
        complete_objective(manager, get_goal_id(this));
        return;
    }
    objects::Rider* skater = local_skater();
    if (!skater)
        return;
    modules::PointTally* score = get_score_of(skater);
    // The points of the trick in progress (+0x14, times the multiplier +0x60) count too.
    const int pending = pending_points(score);
    if (total_score(score) + pending < required)
        fail_objective(manager, get_goal_id(this));
    else
        complete_objective(manager, get_goal_id(this));
}

// 0x005764B0
script::Struct* TrickObjectsGoal::get_object_by_cluster(uint32_t cluster)
{
    script::Array* objects = nullptr;
    args_->fetch_list(kGoalObjects, &objects, 1);
    const int count = static_cast<int>(objects->extent_);
    uint32_t found = cluster; // (kept from the last entry with a cluster_id: an entry without one matches that)
    for (int i = 0; i < count; ++i) {
        auto* object = objects->struct_at(static_cast<uint32_t>(i));
        object->read_hash_value(kClusterId, &found, 1);
        if (found == cluster)
            return object;
    }
    return nullptr;
}

// 0x00576520
void TrickObjectsGoal::get_object_hit(uint32_t cluster)
{
    script::Struct* object = get_object_by_cluster(cluster);
    if (!object || has_won() || is_latched())
        return;
    objects::Rider* skater = local_skater();
    if (!skater)
        return;
    auto* state = core::down_cast<objects::RiderPoseState>(skater->fetch_module(kSkaterState));
    auto* score_component = core::down_cast<objects::RiderPointKeeper>(skater->fetch_module(kSkaterScore));
    modules::PointTally* score = score_component->points_held_;
    // Only while the skater is in the air (+0xE0) or has points pending.
    if (!state->in_trick_move_ && pending_points(score) <= 0)
        return;
    if (!is_operative()) {
        void* manager = objective_board();
        static_cast<GoalManager*>(manager)->engage_objective(get_goal_id(this), false);
    }
    uint32_t flag = 0;
    object->read_hash_value(kFlag, &flag, 1);
    if (!bit_is_on(flag, 0))
        set_flag(flag, 1, 0);
}

} // namespace goals
