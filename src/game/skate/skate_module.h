#pragma once

// The skate module (modules::Skate, 0x3F0 bytes, the singleton at 0x007CE478): the game's top-level state - its skaters
// (a list ordered by object id), the career, the rail manager, the game type, the level (requested, current and the
// one before), the competition / HORSE / movie managers, the game flow and the goal manager, the per-player settings
// and the module's own script; its logic task runs the game each frame (run_frame_logic). Its code: 0x00564C10-0x005691EF
// (skate_module_state.cpp, skate_module_skaters.cpp, skate_rankings.cpp); the network side: skate_net.h.

#include <memory>
#include <cstddef>
#include <cstdint>

#include "engine/lists/chain_start.h"
#include "base/maths/vec4.h"
#include "engine/sys/game_module.h"
#include "engine/memory/mem.h"
#include <gsl/pointers>
#include <array>

namespace objects {
class GameEntity;
class Entity;
class Rider;
class RiderStoryProgress;
class GrindRailHub;
class VideoHub;
struct RiderIdentity;
struct ChecksumSet;
} // namespace objects
namespace prefs {
class Preferences;
}
namespace script {
class Context;
class Script;
class Struct;
} // namespace script
namespace tasks {
class SchedulerTask;
template <typename Owner>
class Task;
} // namespace tasks
namespace online {
class PlayerRecord;
} // namespace online
namespace goals {
class GoalManager;
class MatchFlow;
} // namespace goals
namespace modules {
constexpr std::size_t kScoreGoalCount = 64;
constexpr std::size_t kParticipantCount = 8;
constexpr std::size_t kPad47Bytes = 5;
struct Contest;
class LetterMatch;
} // namespace modules

namespace modules {

class Skate final : public PartUnit {
public:
    Skate(const Skate&) = delete;
    Skate& operator=(const Skate&) = delete;
    // A skater slot's settings (+0x390, 8 of them).
    struct PlayerSettings {
        int unknown0_;   // +0 (the player's index, set by the constructor)
        bool vibration_; // +4 (the skater's vibration component told)
        bool auto_kick_; // +5 (the core physics' +0x2D9)
        bool spin_taps_; // +6 (the core physics' +0x2DA)
        uint8_t spare_byte7_;
    };
    // A score goal (+0x60, 64 of them): a career goal done when the total first reaches `score`.
    struct ScoreGoal {
        int score;
        uint32_t script;
        int goal;
    };

    // The singleton (0x007CE478; null before the module starts). 0x005691A0 (cdecl): made (from the default heap,
    // Calloc mode 1) if there's none and `create`; one more reference either way. 0x00564C10 (cdecl): one reference
    // less - the last deletes it.
    static Skate* sole();
    static Skate* spawn_occurrence(bool create);
    static void discard_occurrence();

    // --- made and destroyed (skate_module_skaters.cpp) ---
    // 0x00568DE0: the constructor: the module, the empty skater list, the logic task, the network tasks, the
    // managers (game flow, goals, career, rails, checksums, split-screen preferences, competition, HORSE, two movie
    // managers), the records' 21 levels, the players' settings.
    Skate();
    // 0x005663A0 / 0x00567BA0: each manager deleted.
    ~Skate() override;
    // 0x00564DC0 / 0x00564C50: the module started (the permanent assets loaded, the logic task added to the main
    // loop's) / stopped (the logic task out).
    void start_callback() override;
    void stop_callback() override;
    // 0x00568C90: the logic task's code - run_frame_logic, the skaters' collisions on the server, king of the hill's
    // clock.
    static void front_end_tick(tasks::Task<Skate>* task);
    // (0x00565750 / 0x00565FC0, the skater list's node's / head's scalar deleting destructors: never called through
    // their tables here - the node is the skater's own, the head the module's.)

    // --- the skaters (skate_module_skaters.cpp) ---
    // 0x0041AD90: the number of skaters in the list (the nodes from the head's next until the head - checked by its
    // data, -1, before a node is checked for null, as the original does).
    int count_riders();
    // 0x005665C0: skater `index` of the list (null past the end).
    objects::GameEntity* get_rider_at(int index);
    // 0x00566520: the first local skater (null: none). 0x00566600: the skater whose object id is `id` (null: none).
    objects::Rider* get_home_rider();
    objects::Rider* get_rider_by_number(uint32_t id);
    // 0x00567180: a skater for `profile` (a local one: the existing local skater - given `id` when it has another -
    // or a new one on the next unused skater heap); its controllers bound, the created tricks and stat goals given
    // back. 0x00565790: a skater removed (its bail board; a client's left script; the local player's created tricks
    // and stat goals kept) and deleted.
    gsl::owner<objects::Rider*> spawn_skater_entity(objects::RiderIdentity* profile, bool local, uint32_t id, int viewport);
    static void drop_skater_from_roster(objects::Rider* skater);
    // 0x00565FF0: the first skater heap no skater uses (from 0 for a local skater, 1 for another).
    int find_free_rider_heap(bool local);
    // 0x00564C60: a skater hidden or shown (`this` unused).
    static void conceal_skater(objects::Rider* skater, bool hide);
    // 0x00565BF0: a skater's shadow made again (detailed for a local skater outside split screen).
    static void set_skater_shadow(objects::Rider* skater);
    // 0x00567090: each local skater's (and its camera's) input bound to its player's controller.
    void bind_controllers();
    // 0x00566100: the restart point for skater `skaterId` (by game mode: the team's, HORSE's, the multiplayer start
    // positions, Player1, any). 0x00566370: a skater moved to `node` (or its restart point). 0x00566980: every
    // local skater moved there and reset.
    uint32_t warp_to_restart(uint32_t skater_id);
    void teleport_to_spawn(objects::Rider* skater, uint32_t node, uint32_t walking);
    // Where the skaters restart: the node, and whether walking.
    struct SpawnChoice {
        uint32_t node{};
        uint32_t walking{};
    };
    void reset_skaters(SpawnChoice spawn_choice);
    // 0x00566BE0: the skaters' collisions (in the games that have them) and king of the hill's crown taken.
    void test_rider_contacts();
    // 0x00567580: whether every skater's run is over (each one still going told to end; HORSE: only the skater
    // taking the run). 0x00567690 / 0x005676E0: HORSE's skater started / completed its first trick.
    bool all_skaters_idle();
    bool first_trick_started();
    bool first_trick_completed();
    // 0x00566F40: the local skater's records of the level (high score, best combo, the longest grind / manual / lip
    // trick, the longest combo).
    void update_records();
    // 0x00567BD0 / 0x00567C00 / 0x00567C30: a player's spin taps / autokick / vibration (and its skater's).
    void set_turn_tap_count(int player, bool on);
    void set_auto_eject(int player, bool on);
    void change_rumble(int player, int on);

    // --- the level (skate_module_state.cpp, skate_module_skaters.cpp) ---
    // 0x00565000: `level`'s script run (the players asked to be ready, the connections' time-outs stretched).
    // 0x00565A00: the level changed to `level` (request_level, the clean-up keeping the skaters, ChangeLevelGameFlow).
    void enter_stage(uint32_t level);
    void change_level(uint32_t level);
    // 0x005666A0: the session's objects deleted (the skaters kept, hidden while the dead are flushed). 0x005668A0:
    // the career's level restarted. 0x00566900: the level reset (the node array, graffiti, goals, reset_level).
    // 0x00566A60: the game launched (the game flow, the king, the flags).
    void tidy_up();
    void restart_level();
    void reset_level();
    void launch_game();
    // 0x00564FD0: the game type loaded into the game mode.
    void set_current_game_type() const;
    // 0x00564F20: the level's objects cleaned up for the park editor.
    void tidy_before_park_build();
    // 0x00565620: the camera's position noted for the objects' updates (none in more than one viewport).
    void prepare_entity_update_data();
    // 0x00567710: one frame: the time of day, the suspend frame, the movies, Game_Update, the proximity nodes, the
    // unplugged controllers.
    void run_frame_logic();
    // (Part of run_frame_logic: the controller unplugged dialog for `skater`'s pad - leaving a net game first.)
    void show_unplugged_dialog(objects::Rider* skater);
    // 0x00564D30 (an object walk's callback): its suspend component's visibility.
    static void update_suspend_visibility(objects::Entity* object, void* data);

    // A timer's beep: the time left, the time it started beeping and the rate.
    struct TimerBeep {
        float time_left_{};
        float start_time_{};
        float rate{};
    };

    // --- small (skate_module_state.cpp) ---
    // 0x00564CA0: the timer's beeps (`script`) speeding up as `timeLeft` runs down from `startTime`.
    static void timer_beep(TimerBeep timer_beep_in, const char* script);
    // 0x00564D50 / 0x00564D60 / 0x00564D80 / 0x00564D90 / 0x00564DA0
    void set_unknown370(int value);
    void advance_match_flow();
    prefs::Preferences* get_split_view_settings();
    goals::GoalManager* get_objective_board();
    void scan_pad_connections();
    // 0x00564F60: the voices' volumes on a pause.
    static void get_freeze(bool paused);
    // 0x00564F80: the profile `params`' integer `skater` names (none, or no params: the current one). 0x00564FC0:
    // profile `index`.
    static objects::RiderIdentity* fetch_account(script::Struct* params);
    static objects::RiderIdentity* fetch_account(int index);
    // 0x005656C0: the career's gap checklist of the level.
    void* fetch_gap_list();
    // 0x005656D0: in a net game, out of the main menu. 0x00565700: in a net mode, or the main menu / CAS asked for.
    [[nodiscard]] int in_net_game_level() const;
    [[nodiscard]] bool in_front_end_or_net_mode() const;
    // 0x00565ED0: the module's script set to `name` (made if there's none). 0x00565F80: deleted (false: none).
    void launch_script(uint32_t name, script::Struct* params);
    bool kill_script();
    // 0x00569580 / 0x00569590: around the network manager's queued scripts.
    void begin_queued_scripts();
    void end_queued_scripts();
    // 0x005695A0: the cheats on, as one value (their checksums OR-ed).
    uint32_t get_cheats();
    // (skate_net_relay.cpp) 0x0056B890: out of the network game - PatchCleanup; a server tells everyone (0x32) and
    // is destroyed with its tasks, a client tells the server (0xB); the players and apps removed.
    void leave_server();
    // 0x0056B990: `player` told which cheats are on (0x82: a count, then the checksums).
    void send_cheats(online::PlayerRecord* player);
    // 0x0056BB70: the scores to each player (0x40: the time left, the cheats, `final`, each other skater's score -
    // its own too when the player doesn't keep it itself), and to a remote player fully in the cheats' check (0x80).
    void broadcast_points(bool final);

    // --- static (cdecl) ---
    // 0x005651D0: the next player observed. 0x005651F0: a game of more than one player (1 / 0). 0x00565680: the
    // network / taunt / split-screen preferences. 0x00565D40: those `params`' prefs names. 0x00565BB0: an object
    // AbsentInNetGames in one (1 / 0). 0x00565DA0: the rails' debug drawing (compiled out).
    static void observe_next_skater();
    static int is_multiplayer();
    static prefs::Preferences* get_player_options(uint32_t name);
    static prefs::Preferences* get_preferences_from_params(script::Struct* params);
    static int should_be_absent(script::Struct* params);
    static void debug_rails();

    // --- script commands ---
    // 0x00565220 SetGameType, 0x00565250 InTeamGame, 0x00565260 ChangeLevel (the players told; the park's map /
    // rails and created goals streamed to them), 0x005654B0 LaunchLevel, 0x005654D0 RequestLevel, 0x005655B0
    // InitSkaterHeaps, 0x00566E70 Retry, 0x00566EA0 LaunchGame, 0x00566ED0 ResetLevel, 0x00568120
    // FillRankingScreen (skate_rankings.cpp).
    static bool cfunc_set_game_type(script::Struct* params, script::Context context);
    static bool cfunc_in_team_game(script::Struct* params, script::Context context);
    static bool cfunc_change_level(script::Struct* params, script::Context context);
    static bool cfunc_launch_level(script::Struct* params, script::Context context);
    static bool cfunc_request_level(script::Struct* params, script::Context context);
    static bool cfunc_init_skater_heaps(script::Struct* params, script::Context context);
    static bool cfunc_retry(script::Struct* params, script::Context context);
    static bool cfunc_launch_game(script::Struct* params, script::Context context);
    static bool cfunc_reset_level(script::Struct* params, script::Context context);
    static bool cfunc_fill_ranking_screen(script::Struct* params, script::Context context);

    // (the list's node at `index` - lists::Foremost's fetch_entry; the skater there, null past the end)
    memory::ListNode* skater_node(uint32_t index);
    objects::Rider* get_skater_at(uint32_t index);

    // The fields other code reads and sets.
    std::array<std::unique_ptr<tasks::Task<Skate>>, 2>& server_tasks() { return server_tasks_; }
    [[nodiscard]] const std::array<std::unique_ptr<tasks::Task<Skate>>, 2>& server_tasks() const { return server_tasks_; }
    std::unique_ptr<objects::RiderStoryProgress>& career() { return career_; }
    [[nodiscard]] const std::unique_ptr<objects::RiderStoryProgress>& career() const { return career_; }
    memory::HeapUnique<objects::GrindRailHub>& rail_manager() { return rail_manager_; }
    [[nodiscard]] const memory::HeapUnique<objects::GrindRailHub>& rail_manager() const { return rail_manager_; }
    [[nodiscard]] const lists::Foremost& riders() const { return riders_; }
    [[nodiscard]] bool running_queued_scripts() const { return running_queued_scripts_; }
    bool& run_over() { return run_over_; }
    [[nodiscard]] bool run_over() const { return run_over_; }
    std::array<ScoreGoal, kScoreGoalCount>& score_goals() { return score_goals_; }
    [[nodiscard]] const std::array<ScoreGoal, kScoreGoalCount>& score_goals() const { return score_goals_; }
    memory::HeapUnique<modules::Contest>& competition() { return competition_; }
    [[nodiscard]] const memory::HeapUnique<modules::Contest>& competition() const { return competition_; }
    memory::HeapUnique<modules::LetterMatch>& horse() { return horse_; }
    [[nodiscard]] const memory::HeapUnique<modules::LetterMatch>& horse() const { return horse_; }
    std::unique_ptr<objects::VideoHub>& movies() { return movies_; }
    [[nodiscard]] const std::unique_ptr<objects::VideoHub>& movies() const { return movies_; }
    std::unique_ptr<objects::VideoHub>& object_movies() { return object_movies_; }
    [[nodiscard]] const std::unique_ptr<objects::VideoHub>& object_movies() const { return object_movies_; }
    [[nodiscard]] uint32_t pending_level() const { return pending_level_; }
    [[nodiscard]] uint32_t level() const { return level_; }
    [[nodiscard]] uint32_t previous_level() const { return previous_level_; }
    [[nodiscard]] uint32_t level_params() const { return level_params_; }
    void set_level_params(uint32_t value) { level_params_ = value; }
    [[nodiscard]] bool new_record() const { return new_record_; }
    void set_new_record(bool value) { new_record_ = value; }
    std::unique_ptr<goals::MatchFlow>& game_flow() { return game_flow_; }
    [[nodiscard]] const std::unique_ptr<goals::MatchFlow>& game_flow() const { return game_flow_; }
    memory::HeapUnique<goals::GoalManager>& goal_manager() { return goal_manager_; }
    [[nodiscard]] const memory::HeapUnique<goals::GoalManager>& goal_manager() const { return goal_manager_; }
    [[nodiscard]] const std::array<PlayerSettings, kParticipantCount>& participants() const { return participants_; }
    [[nodiscard]] float forced_stat() const { return forced_stat_; }
    void set_forced_stat(float value) { forced_stat_ = value; }
    bool& get_debug_rails() { return debug_rails_; }
    [[nodiscard]] bool get_debug_rails() const { return debug_rails_; }
    [[nodiscard]] bool no_camera() const { return no_camera_; }
    maths::Vec4& viewer_pos() { return viewer_pos_; }
    [[nodiscard]] const maths::Vec4& viewer_pos() const { return viewer_pos_; }

private:
    std::unique_ptr<tasks::Task<Skate>> update_job_;         // +0x14 (front_end_tick)
    std::array<std::unique_ptr<tasks::Task<Skate>>, 2> server_tasks_;                  // +0x18 (SkateNet::create_network_tasks)
    std::unique_ptr<objects::RiderStoryProgress> career_;    // +0x20
    memory::HeapUnique<objects::GrindRailHub> rail_manager_; // +0x24
    lists::Foremost riders_{}; // +0x28 (their nodes' data the skaters, in object id order)
    bool watch_for_unplug_{false}; // +0x3C
    std::array<uint8_t, 3> pad3d_{};
    int unplugged_frames_{0};       // +0x40
    bool running_queued_scripts_{}; // +0x44
    bool level_launched_{false};    // +0x45 (enter_stage)
    bool run_over_{};             // +0x46
    std::array<uint8_t, kPad47Bytes> pad47{};
    static constexpr uint32_t kStartMatchStyle = 0x34861A16; // freeskate
    uint32_t match_style_{kStartMatchStyle}; // +0x4C (freeskate)
    bool movie_camera_{};  // +0x50 a movie's camera in use
    std::array<uint8_t, 3> pad51{};
    memory::HeapUnique<objects::ChecksumSet> checksums_;          // +0x54 (0x404 bytes)
    std::unique_ptr<prefs::Preferences> splitscreen_preferences_; // +0x58
    bool stage_switched_{};                                       // +0x5C
    std::array<uint8_t, 3> pad5d_{};
    std::array<ScoreGoal, kScoreGoalCount> score_goals_{};                         // +0x60
    memory::HeapUnique<modules::Contest> competition_;    // +0x360
    memory::HeapUnique<modules::LetterMatch> horse_;      // +0x364
    std::unique_ptr<objects::VideoHub> movies_;           // +0x368
    std::unique_ptr<objects::VideoHub> object_movies_;    // +0x36C
    int unknown370_{};                                    // +0x370
    uint32_t pending_level_{};                            // +0x374
    uint32_t level_{};                                    // +0x378
    uint32_t previous_level_{};                           // +0x37C
    uint32_t level_params_{};                             // +0x380 the level's structure (its name)
    bool new_record_{};                                   // +0x384 a record was set by the last UpdateRecords
    std::unique_ptr<goals::MatchFlow> game_flow_;         // +0x388
    memory::HeapUnique<goals::GoalManager> goal_manager_; // +0x38C
    std::array<PlayerSettings, kParticipantCount> participants_{};                    // +0x390
    float forced_stat_{};                                 // +0x3D0 every skater's stats, forced (unless 0)
    bool debug_rails_{};                                  // +0x3D4
    script::Script* own_script_;                          // +0x3D8
    bool no_camera_{};         // +0x3DC (prepare_entity_update_data: no camera to measure from)
    maths::Vec4 viewer_pos_{}; // +0x3E0
};

// 0x00567C70: elimiskate's rankings (FillRankingScreen's: by the time each player was knocked out).
void fill_elimination_ranking();

} // namespace modules
