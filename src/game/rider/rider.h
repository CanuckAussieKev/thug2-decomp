#pragma once

#include <memory>
#include <cstddef>
#include <cstdint>
#include <array>

#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/objects/entity.h"
#include "base/core/checksums.h"
#include "game/rider/rider_created_tricks.h"
#include "game/rider/rider_trick_editor.h"
#include "game/rider/rider_traits.h"

// Pieces of the skater object (GameObjects::Rider, a composite object; 0x0057FE30-0x00580BD8): its stats (overrides at
// +0x290, the cheats and online rules on top), the physics structure for what it rides (skater_physics, or a
// vehicle's), values scaled by a stat (pair ranges, the switch / difficulty pairs, limits), sparks, its camera and
// number, matching a profile's description, the CAT and chapter info a save carries, pausing for its physics control,
// and the light direction its shadow is cast by. The rest of the skater's code is elsewhere (and not decompiled
// yet): only the fields these use are named.
namespace script {
class Struct;
}
namespace goals {
class CreateATrick;
}

namespace skater {
class SkaterPhysicsModule;
} // namespace skater
namespace modules {
class PointTally;
}
namespace objects {
constexpr std::size_t kProjectileSlots = 5;
constexpr int kProjectileCount = 5; // the same, as an int count (the loops)
constexpr std::size_t kStatOverrideCount = 10;

struct RiderIdentity;
class EntityHub;
class AnimBlenderComponent;
class AttributeTracker;
class FootMovement;
class GroundShadowModule;
class MeshInstanceModule;
class MoveExecutor;
class PadInputModule;
class PadRumbleModule;
class RiderBoardPhysics;
class RiderFrameLog;
class RiderGapTracker;
class RiderPointKeeper;
class RiderSfxPlayer;
class SkaterVehicleSoundComponent;

// The original's vtable (0x0064EF48) is GameEntity's with its own destructor and invoke_method_by_name and nine more
// slots: +0x2C get_freeze, +0x30 unpause, +0x34 init_from_profile, +0x38 reset_input, +0x3C jump_to_spawn, +0x40
// glide_to_node, +0x44 reset_state, +0x48 set_up_network, +0x4C run_state_script. Nothing derives from the skater:
// those are plain methods here.
class Rider final : public GameEntity {
public:
    Rider(const Rider&) = delete;
    Rider& operator=(const Rider&) = delete;
    // Not original - the fields' offsets checked against the original's at compile time (rider.cpp).
    static void check_layout();
    // 0x0057FE30: stat `index`'s override (+0x290), kept within 0-10 when 0x005A1180 says so.
    void set_stat_override(int index, float value);
    // 0x00580490: stat `index` (the override, the default stats if none, the skate module's forced value; online
    // limits; CHEAT_STATS_13: 15; +3 with the score's bonus flag).
    float fetch_tally(int index);
    // 0x005805B0: a value from `params`: its unnamed pair scaled by the stat named by its unnamed integer (10 if
    // none), times its switch pair (by the switch stat, 0-1) when riding switch, times its diff pair's first or
    // second for game difficulty 0 or 2, kept within its limit (the pair's direction says which side).
    float get_scaled_value(script::Struct* params);
    // 0x00580790: the physics structure's `name` structure (or the global one) as get_scaled_value's. 0x005807F0:
    // `params`' `name` structure's.
    float get_scaled_physics(uint32_t name);
    float get_scaled_param(uint32_t name, script::Struct* params);

    // 0x0057FEB0: its viewport's camera. 0x0057FEC0: its skater number. 0x00580880: its skater camera (skatercam0
    // or skatercam1, by viewport).
    [[nodiscard]] void* obtain_active_lens() const;
    [[nodiscard]] int get_skater_number() const;
    [[nodiscard]] Entity* get_skater_camera() const;
    // 0x00580030: the core physics' display matrix (its +0x18).
    maths::Mat4* get_draw_matrix();
    // 0x00580040: its +0x238 block. 0x00580050: the flag at +0x2C4. 0x00580480: its score.
    memory::ListNode* get_skater_link();
    [[nodiscard]] bool get_flag_2c_4() const;
    modules::PointTally* get_points_total();
    // 0x0057FFE0 / 0x00580000: sparks on (SkaterParticles_SparksOn) / off (SkaterParticles_SparksOff, if on).
    void sparks_on();
    void sparks_off();
    // 0x00580150: the 70 entries of its +0x28C list from `params`.
    void load_list_44b_66a_9f(script::Struct* params);
    // 0x005801B0: the chapter arrays (CHAPTER_STATUS, CHAPTER_INTROS_VIEWED, CHAPTER_OUTTROS_VIEWED) added to `save`.
    // 0x00580210: copied back from `save`.
    static void write_chapter_info(script::Struct* save);
    static void read_chapter_info(script::Struct* save);
    // 0x005802C0: 0x0042BD60 with this and `value`.
    void set_42b_d60(float value);
    // 0x00580830: moved to a random restart (0-7, through the skate module; up to 20 tries for one other than the
    // last): jump_to_spawn({.node = node, .walking = walking}).
    void move_to_random_restart();
    // 0x005808A0: its pause state (0 running, 1 frozen, 2 physics paused): the game paused or the physics control
    // told.
    void set_pause_state(int state);
    // 0x005809C0: its shadow's light direction (the model's light, clamped below the horizon), faded in or out
    // over 16 frames as it is in the air or not.
    void update_shadow_light();
    // 0x00580BE0: the ten stats from `profile` (or, in classic mode, the skate module's classic stats).
    void set_stats_from_profile(RiderIdentity* profile);
    // 0x00580EB0: its looks and identity from `profile` (in its skater heap): push style, stance, pro, display name,
    // male, skate style, first name, name; its stance flag set again and the flip-and-rotate component told.
    void update_from_profile(RiderIdentity* profile);
    // 0x00580FF0: its score, a component (0x589E5FB3), set_up_network; the 24 input flags pressed then released; its
    // +0x264's +0x20 reset; the script 0xAE5438AF.
    void reset_input();
    // 0x005810E0: its state reset (physics control, flip and rotate, its +0x258's resets, the balance displays off,
    // the shadow placed, 0x21FC3301's +0x18 -1).
    void reset_state();
    // 0x005811E0: its net client's id told to its flip-and-rotate component, its +0x250 told; then +0x24C's
    // 0x00419A20.
    void set_up_network();
    // 0x00581260: the board element for `type` (four kinds; their structures say the texture, sizes, model swaps...).
    void show_board_element(uint32_t type);
    // 0x005814D0 / 0x00581510 / 0x00581550: a component's reset (not local); hidden; shown.
    void reset_27c();
    void conceal_skater();
    void show_skater();
    // 0x00581580: its display name (online: the player's).
    const char* fetch_shown_name();
    // 0x005815C0: moved to the node named `node` (no node: only the orientation reset).
    void glide_to_node(uint32_t name);
    // 0x00581760 / 0x005817F0: paused (the displays off, a script, the pad's vibration, the sounds) / unpaused.
    void get_freeze();
    void unpause();
    // 0x00581850: set up from `profile`: a local skater's camera (found or made from skatercam_composite_structure),
    // its script, its components (motion, an anim set's, skeleton, model - its geometry loaded in the skater's
    // geometry heap -, shadow, a local skater's input / vibration / two more), Finalize, a local skater's pad
    // settings and SkaterInit, its profile and stats, ... its trick infos and projectiles, its controls' textures.
    void prepare_all(RiderIdentity* profile);
    // 0x005822A0: Init, then a local skater's network message handlers (when 0x005A11B0 says so), and never suspended.
    void init_from_profile(RiderIdentity* profile);
    // 0x005823A0: its state script 0xE740F458 started (with Walk / 0x7B738580 flags), run once if local.
    void run_state_script(bool walk, bool other);
    // 0x00582510: its script switched to `name` (flip-and-rotate told first) and run once.
    void switch_to_script(uint32_t name, script::Struct* params);
    // 0x00582740: the +0x28C list's 70 entries written.
    void write_list_44b_66a_9f(script::Struct* save);
    // 0x00582800: a projectile launched (LaunchProjectile) from a wrist, at the nearest target ahead (projectile
    // proximity components within Physics_Projectile_Targeting_Dist / _Angle; peds not yet shot, at a bone they
    // choose) when there's one beyond 100.
    void launch_projectile();
    // 0x00582D90: its skater-list node, local or not, base components, type 0xBAF9B341, number and id (skaters 0 and 1
    // get tracker aliases), registered with `manager` (list priority -500), its viewport, and its components (a local
    // skater's full set, a remote one's few). The last argument isn't used.
    // A rider's object id and its skater number.
    struct RiderSlot {
        uint32_t id{};
        int skater_number_{};
    };
    Rider(int viewport, EntityHub* manager, bool local, RiderSlot rider_slot, int unused);
    // 0x00583400: a local skater's +0x270 and 0x589E5FB3 reset, 0xF27A294D's, the anim blender's; its script deleted.
    void reset_components();
    // 0x00583470: restarted (a walking skater back to skating, the script's exit scripts, its physics, its camera,
    // reset_state, reset_components, set_up_network, run_state_script with `a` and `b`, 0xB1D5A3E1 told).
    void relaunch(uint32_t a, uint32_t b);
    // 0x005835D0: its script member functions (KillSkater, AddSkaterToWorld, SkaterIsNamed, ShouldMongo, the
    // sparks, SetCustomRestart / SkipToCustomRestart, IsLocalSkater, GetCameraId, fetch_tally, GetScriptedStat,
    // MoveToRandomRestart, PlaceBeforeCamera, GetSkaterNumber...); the rest the composite object's.
    bool invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;
    // (the code SkipToCustomRestart and SetCustomRestart share)
    void skip_to_custom_restart();
    // Where a skater restarts: the node, and whether walking.
    struct SpawnChoice {
        uint32_t node{};
        uint32_t walking{};
    };

    // 0x00583DA0: skipped to the restart node `node` (none: where it is), walking or not: a local skater's input and
    // pad reset, glide_to_node with the node, Restart, the camera updated, the node's TriggerScript run; teleported;
    // in three game modes the server told (message 0x57).
    void jump_to_spawn(SpawnChoice spawn_choice);
    // 0x00583FB0 (0x005840E0 the deleting one): the pad's vibration stopped, its components reset, its exit script,
    // its created tricks deleted, the second viewport's camera marked dead, out of the skater list, then GameEntity's.
    ~Rider() override;

    // A launched projectile (the skater keeps five).
    struct Projectile {
        uint32_t name; // projectile_0 ... projectile_4
        bool active;
        std::array<uint8_t, 3> pad5_;
        maths::Vec4 pos; // +0x08 (the projectile component's position; w unused)
    };

    // The fields other code reads and sets.
    RiderTraits& traits() { return traits_; }
    [[nodiscard]] const RiderTraits& traits() const { return traits_; }
    CreatedTricks& created_tricks() { return created_tricks_; }
    [[nodiscard]] const CreatedTricks& created_tricks() const { return created_tricks_; }
    TrickEditorState& cat_editor() { return cat_editor_; }
    [[nodiscard]] const TrickEditorState& cat_editor() const { return cat_editor_; }
    std::array<Projectile, kProjectileSlots>& projectiles() { return projectiles_; }
    [[nodiscard]] const std::array<Projectile, kProjectileSlots>& projectiles() const { return projectiles_; }
    [[nodiscard]] PadInputModule* pad_input() const { return pad_input_; }
    [[nodiscard]] RiderPointKeeper* score_holder() const { return score_holder_; }
    [[nodiscard]] RiderFrameLog* state_history() const { return state_history_; }
    [[nodiscard]] skater::SkaterPhysicsModule* core_physics() const { return core_physics_; }
    [[nodiscard]] FootMovement* walk() const { return walk_; }
    [[nodiscard]] int view_slot() const { return view_slot_; }
    [[nodiscard]] const maths::Vec4& custom_restart_pos() const { return custom_restart_pos_; }
    void set_custom_restart_pos(const maths::Vec4& value) { custom_restart_pos_ = value; }
    maths::Mat4& custom_restart_matrix() { return custom_restart_matrix_; }
    [[nodiscard]] const maths::Mat4& custom_restart_matrix() const { return custom_restart_matrix_; }
    void set_custom_restart_matrix(const maths::Mat4& value) { custom_restart_matrix_ = value; }
    [[nodiscard]] bool custom_restart_walking() const { return custom_restart_walking_; }
    void set_custom_restart_walking(bool value) { custom_restart_walking_ = value; }
    [[nodiscard]] uint32_t unknown31c() const { return unknown31c_; }
    void set_unknown31c(uint32_t value) { unknown31c_ = value; }
    [[nodiscard]] bool is_halted() const { return is_halted_; }

private:
    RiderTraits traits_;      // +0x108-+0x16B what it keeps of its profile
    uint32_t last_restart_{}; // +0x16C
    bool sparks_{};           // +0x170
    std::array<uint8_t, 3> pad171{};
    int shadow_fade_{}; // +0x174
    std::array<uint8_t, 4> unknown178_{};
    CreatedTricks created_tricks_; // +0x17C-+0x1A7 the created tricks
    TrickEditorState cat_editor_;  // +0x1A8-+0x1BF the create-a-trick editor's state

    std::array<Projectile, kProjectileSlots> projectiles_{}; // +0x1C0
    memory::ListNode skater_link_{};      // +0x238 its node in the skate module's skater list (data = itself)
    AnimBlenderComponent* anim_blender_;  // +0x24C
    MeshInstanceModule* model_component_; // +0x250
    GroundShadowModule* shadow_;          // +0x254
    MoveExecutor* trick_;                 // +0x258 SkaterTrick
    PadRumbleModule* vibration_;          // +0x25C (local skaters only)
    std::array<uint8_t, 4> unknown260_{};
    PadInputModule* pad_input_;                  // +0x264 (local skaters only)
    RiderSfxPlayer* sound_;                      // +0x268 SkaterSound (0x187574FA)
    SkaterVehicleSoundComponent* vehicle_sound_{}; // +0x26C SkaterVehicleSound (0x1859F772)
    RiderGapTracker* gap_tracker_;               // +0x270 SkaterGap
    std::array<uint8_t, 4> unknown274_{};
    RiderPointKeeper* score_holder_;            // +0x278 SkaterScore
    RiderFrameLog* state_history_;              // +0x27C SkaterStateHistory
    skater::SkaterPhysicsModule* core_physics_; // +0x280
    RiderBoardPhysics* physics_control_;        // +0x284 SkaterPhysicsControl (0xD9151D4D)
    FootMovement* walk_;                        // +0x288 Walk (0x726E85AA)
    AttributeTracker* stats_;                   // +0x28C SkaterStatsManager (0x4FF8DAC6)
    std::array<float, kStatOverrideCount> stat_overrides_{};                // +0x290
    int skater_number_;                         // +0x2B8
    int view_slot_;                             // +0x2BC
    int pause_state_{};                         // +0x2C0
    bool flag_2c_4_{false};                     // +0x2C4
    std::array<uint8_t, 3> pad2c5_{};
    maths::Vec4 custom_restart_pos_{};    // +0x2C8 (SetCustomRestart)
    maths::Mat4 custom_restart_matrix_{}; // +0x2D8
    bool custom_restart_walking_{};       // +0x318
    bool custom_restart_set_{};           // +0x319
    std::array<uint8_t, 2> pad31a_{};
    uint32_t unknown31c_; // +0x31C
    bool is_halted_{};    // +0x320
    std::array<uint8_t, 3> pad321{};
};

// 0x005802E0: the physics structure for what `skater` rides (null or not a skater: skater_physics).
script::Struct* get_physics_structure(GameEntity* skater);
// 0x00580400 / 0x00580440: a physics value from `skater`'s structure, else the global script value.
float read_tuning_float(checksums::Name tuning_name, int assert, GameEntity* skater);
int read_tuning_int(checksums::Name tuning_name, int assert, GameEntity* skater);

} // namespace objects
