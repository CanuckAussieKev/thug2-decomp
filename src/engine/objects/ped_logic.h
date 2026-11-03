#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include <gsl/pointers>
#include <array>

namespace script {
class Script;
class Struct;
} // namespace script

namespace objects {

class ObjectGroup;

// The motion component's last ground triangle (KinematicsModule +0x40): three corners.
struct GroundTriangle {

    std::array<maths::Vec4, 3> corners;
};

// 0x004EDC10 (not a Ghidra function): the copy assignment (ECX = this, RET 4; returns this).
GroundTriangle& get_assign(GroundTriangle& triangle, const GroundTriangle& source);
static_assert(sizeof(GroundTriangle) == 0x30, "layout must match the original");

// PedestrianBrainModule::flag_bits_ (+0x20)
constexpr uint32_t kPedFlagBoardHideable = 0x1;   // its profile's board has a desc_id: hide_boardgeom hides it
constexpr uint32_t kPedFlagMoving = 0x2;          // following its path (Ped_StartMoving / Ped_StopMoving)
constexpr uint32_t kPedFlagGrinding = 0x4;        // Ped_SetIsGrinding
constexpr uint32_t kPedFlagVertNext = 0x8;        // the node it's heading for has a vert SkateAction
constexpr uint32_t kPedFlagVertLast = 0x10;       // the node it came from had one
constexpr uint32_t kPedFlagVertAfter = 0x20;      // the node after next has one
constexpr uint32_t kPedFlagBailing = 0x40;        // Ped_SetIsBailing
constexpr uint32_t kPedFlagGrindBailing = 0x80;   // Ped_SetIsBailing Grind (bailing off a grind)
constexpr uint32_t kPedFlagCrouch = 0x100;        // a jump at the next node: crouch when near (STATE_jumpcrouch)
constexpr uint32_t kPedFlagSpinning = 0x200;      // a jump's spin (spin_angle_) in progress
constexpr uint32_t kPedFlagVertRotating = 0x400;  // a jump's turn over a spine in progress
constexpr uint32_t kPedFlagJumpingToNode = 0x800; // JumpToNextNode
constexpr uint32_t kPedFlagStopping = 0x1000;     // the stop state: ped_skater_start_moving runs when it moves
constexpr uint32_t kPedFlagSoundState3 = 0x2000;  // (not set here) the sounds' state 3 when not moving
constexpr uint32_t kPedFlagVertLip = 0x8000;      // the node it's heading for has a Vert_Lip action
constexpr uint32_t kPedFlagBoardHidden = 0x20000; // its board is hidden

// The PedLogic component (type 0x6EFDDFB8 "pedlogic", vtable 0x0064B7A8, 0x1CC bytes; made by 0x004F0B10): a ped's
// brain - walkers and the skating peds. It runs a logic state from a state table (Ped_StateLogic_Table, the object's
// UniqueStateTable tag, SkaterPed_StateTable...: each state a structure naming its script_ped_state, the script the
// object runs, and its ped_update_type, the move update run each frame), follows its path of waypoint nodes
// (choosing among their links by Class / Type, SkateAction and Priority), steers round other peds on the path and
// away from the path's edges, and for skaters does the jumps, grinds, lip tricks and bails the nodes' SkateActions
// ask for. Its script member functions are in the table at 0x00683730 (filled by 0x00642B90): Ped_SetLogicState ...
// ped_get_voice_prefix.
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 2 configure_from_data; 3 / 4 / 9 0x005480A0, 5 0x004E3F90, 6
// 0x0048DCD0, 7 0x0045D760, 8 / 10 0x005BAB90, 11 0x0042BB20 (shared), 12 invoke_method_by_name 0x004ECFD0; then its
// own: 13 (+0x34) the generic move update 0x004F2A10, 14 (+0x38) the skater move update 0x004F2AD0, 15 (+0x3C) the
// no-move update 0x004ED4A0.
class PedestrianBrainModule final : public ObjectModule {
public:
    PedestrianBrainModule(const PedestrianBrainModule&) = delete;
    PedestrianBrainModule& operator=(const PedestrianBrainModule&) = delete;
    static constexpr uint32_t kType = 0x6EFDDFB8;

    // 0x004F0B10 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static gsl::owner<ObjectModule*> fabricate();
    // 0x004ECFB0 (registered with Create; not a Ghidra function): the member function table 0x00683730 (24-byte
    // records) made script symbols.
    static void publish_method_table();
    // 0x004ECFD0 (vtable slot 12): 1 (done, nothing else) for the member function 0x315D9ED4, 2 (not ours) for any other.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;
    // 0x004EE4F0: its type; no avoidance or bias, the position history at the
    // origin, ped_skater_stick_dist_above / below, the states "" (their checksums), no aux script, the next waypoint at
    // the origin.
    PedestrianBrainModule();
    // 0x004F2120: the aux script deleted, then the base's body.
    ~PedestrianBrainModule() override;
    int invoke_method(const script::MemberFunctionEntry* entry, script::Struct* params, script::Script* script) override
    {
        return dispatch_script_method(entry, params, script); // (slot 11: 0x0042BB20, shared)
    }
    // 0x004ED2C0 (vtable slot 2): female if SkeletonName is Ped_Female; kPedFlagBoardHideable if profile's board has
    // a desc_id; the start node (Name, asserted) and its position; up = the object's up row (all three); DistAbove /
    // DistBelow (the stick distances); the display matrix kept.
    void configure_from_data(script::Struct* params) override;
    // 0x004F2190 (vtable slot 1): the frame time, then the move update ped_update_type names; the aux script run.
    void tick_frame() override;

    // 0x004ED140: the logic state (a state table's member name; shot, ...).
    uint32_t get_logic_state() { return cur_phase_; }

    // 0x004F0B70: the logic state `state` of `table` (0: the object's UniqueStateTable tag, else Ped_StateLogic_Table;
    // a state the table hasn't is looked for in Ped_StateLogic_Table) set: the old state's exit_state_action (a script
    // run, or wait_anim_finished: unless the new state is high_priority, the new one is delayed - DelayedState /
    // DelayedTable tags - and STATE_DELAY_waitanim runs), the board shown / hidden by hide_boardgeom, the object's
    // script switched to script_ped_state with `params`, the aux script killed, ped_initialize_movement run. False if
    // the object hasn't its ped_init tag or the state isn't found.
    bool change_phase(uint32_t state, uint32_t table, script::Struct* params);
    // 0x004ED460: ped_initialize_movement run on the object; moving if the update type is the generic or the skater
    // move update.
    void initialize_movement();
    // 0x004EDD00: the board (the model's geom board) hidden or shown, if kPedFlagBoardHideable (else false).
    bool hide_board(int hide);
    // 0x004EDC60: the looping sound's terrain (+0x74, marked changed) and the skater sound's (+0x18) set to `state`.
    void set_sound_state(int state);
    // 0x004EDCB0: the looping sound's terrain (+0x60, marked changed) and the skater sound's (+0x1C, +0x1D) set.
    void set_sound_terrain(uint8_t terrain);
    // 0x004F2240: the aux script (made if there's none) set to `script` with `params` and run once; deleted if it
    // ends at once. Nothing for no script.
    void activate_aux_script(uint32_t script, script::Struct* params);
    // 0x004EFB10: a new structure with `params`, then the object's tags, appended (the caller deletes it).
    gsl::owner<script::Struct*> get_state_params(script::Struct* params);
    // 0x004ED4F0: the object's position pushed onto the position history (the oldest dropped).
    void push_position();
    // 0x004F1E10: a bail (STATE_bailgrind if grinding, else STATE_bail, of SkaterPed_StateTable; the stored max
    // velocity given back to the motion component if there is one), unless the motion component's flag 0x100 is set
    // (then false). Grinding, spinning and the spine turn end.
    bool get_bail();

    // --- the path (ped_logic_path.cpp) ---
    // 0x004ED0E0: a chance by the node's weight `weight_name` (asserted): above 0.99 always, else weight in 1000.
    bool get_chance(script::Struct* node, uint32_t weight_name);
    // 0x004ED8A0: the node after the next one, among the next one's links: not back the way it came (unless it's the
    // only link and isn't a vert grab), Class Waypoint and Type PedAI, not a stop action's link unless coming from one,
    // turning at least ped_min_inner_path_angle - a Normal priority one at random (or a Low one by
    // ped_low_priority_waypoint_probability), else the sharpest turn; its position.
    void choose_node_after();
    // 0x004EEFA0: a waypoint node's actions, each by its chance: the motion component's max speed changed
    // (AdjustSpeedPercent), a script spawned on the object (RunScriptName).
    void run_waypoint_actions(script::Struct* node);
    // 0x004EFD50: a new structure (StateParams) with a jump's parameters: straight up (jumpSpeed from JumpHeight and
    // ped_skater_jump_gravity) or to the next node (heading, jumpSpeed, jump_time; land_height); the spin (SpinAngle or
    // random half turns, SpinDirection) and the spine transfer's turn (should_flip). The caller deletes it.
    gsl::owner<script::Struct*> leap_node_params(script::Struct* params, bool to_node);
    // 0x004F2570: the next waypoint reached: the path moved on (the vert flags, the crouch for a jump, the node after
    // chosen), the reached node's actions and, for a skating ped's node, its SkateAction (not after a vert grab).
    void reach_waypoint();
    // 0x004F2340: the state a skating ped's node's SkateAction starts (none while bailing).
    void do_skate_action(script::Struct* node);
    // The states do_skate_action starts (SkaterPed_StateTable's), with StateParams / leap_node_params of the node:
    void start_grind(script::Struct* node);           // 0x004F1A90 STATE_grind (the node's TerrainType's sound)
    void end_grind(script::Struct* node);             // 0x004F1B10 STATE_grindoff, if grinding
    void get_jump(script::Struct* node, bool vert);   // 0x004F1B40 STATE_jump / STATE_jumpvert
    void get_lip_trick(script::Struct* node);         // 0x004F1BF0 STATE_tricklip (upright: lip_trick_update)
    void get_manual(script::Struct* node);            // 0x004F1C40 STATE_manual
    void manual_down(script::Struct* node);           // 0x004F1C90 STATE_manualdown
    void grab_trick(script::Struct* node, bool vert); // 0x004F1CE0 STATE_trickgrab (is_vert, jump_time, is_jumping)
    void halt(script::Struct* node);                  // 0x004F1DC0 STATE_stop
    void roll_off(script::Struct* node);              // 0x004F1F30 STATE_rolloff (no parameters)
    void flip_trick(script::Struct* node, bool vert); // 0x004F1F50 STATE_trickflip
    // 0x004F1E80 (not a Ghidra function): a bail off the grind it's on (STATE_bailgrind; the node after next becomes
    // the next node's stop link). False if it isn't grinding.
    bool bail_from_grind();

    // 0x004ED080: whether the node named `node` has a vert SkateAction (Vert_Grab, Vert_Lip, Vert_Grind, 0xDA0723DA,
    // Vert_Flip, Vert_Jump). There must be such a node: the original reads through a null otherwise.
    bool is_vert_node(uint32_t node);

    // --- the movement (ped_logic_move.cpp) ---
    // 0x004ECFF0: the lasting bias decayed: each quarter times the script float 0x98CD0C8C, set to 0 when the product
    // is below ped_min_bias (negative ones too).
    void decay_bias();
    // An avoidance: its angle, its strength, the distance and the range it works over.
    struct Avoidance {
        float angle{};
        float strength{};
        float distance{};
        float range{};
    };

    // 0x004ED5E0: `strength` scaled by how far into `range` the other is, spread over the bias quarters near `angle`
    // (radians) as cos(quarter - angle), each kept in -1..1.
    void add_avoidance(Avoidance avoidance);
    // A steering angle and its weight.
    struct Steer {
        float angle{};
        float weight{};
    };

    // 0x004ED680: `weight` spread likewise over this frame's steering quarters.
    void add_steer(Steer steer);
    // 0x004ED710 / 0x004ED7D0: the steering's sideways / forward part (-1..1; 0 below ped_min_bias).
    float steer_sideways();
    float steer_forward();
    // 0x004EE650: facing `direction` (flat), upright: the display matrix, the orientation and the kept matrix.
    void face_along(const maths::Vec4* direction);
    // 0x004EE7D0: the bias away from each other ped of the path near enough (ped_avoid_ped_range, within
    // ped_max_y_distance_to_ignore up or down) that's ahead or slower; head on, to the side. Whether any was.
    bool avoid_peds();
    // 0x004EEAC0: steering back towards the path's line by how far off it it is (ped_max / min_distance_to_path).
    void keep_to_path();
    // 0x004EEDC0: the object moved by the steering at the motion component's speed (the position history pushed).
    void move_steered();
    // 0x004EEEC0: steering towards `target` with `weight`.
    void steer_towards(maths::Vec4 target, float weight);
    // 0x004F2780: steering towards the next waypoint, and near it (ped_[skater_]fade_target_bias_max_distance_square)
    // towards the one after over the frames the corner takes; the waypoint reached when the turn is done (or, at a
    // dead end, when near enough). Whether it's turning.
    bool turn_towards_node_after();
    // 0x004EF350: facing where it moved from `old_pos` (a walker turning at most 3 degrees a frame - more on sharp
    // turns; a skater along its path to a lip).
    void face_movement(maths::Vec4 old_pos);
    // 0x004EF740: the ground hit between `start` and `end`: the point, the face's triangle, its terrain's sounds.
    bool probe_ground(const maths::Vec4* start, const maths::Vec4* end, maths::Vec4* point, GroundTriangle* triangle);
    // 0x004EF930 (not a Ghidra function): the orientation's across row rebuilt; the object put on the ground found
    // from stick_above_ above to stick_below_ below (the motion component's ground triangle kept).
    void stick_to_ground_ped();
    // 0x004EF090 (not a Ghidra function): the up it blends to set from the ground triangle (blending at 6 a second),
    // the display matrix turned to it.
    void align_to_ground();
    // 0x004F01C0: one frame of a jump's spin and spine turn, the display matrix rotated from the take-off's.
    void spin_update();
    // 0x004F03C0: what's `range` ahead along `direction` (flattened to the up plane): a ped within 30 degrees (3, its
    // position), the world (1, the hit point), nothing (0).
    int probe_ahead(maths::Vec4 direction, float range, maths::Vec4* out);
    // 0x004F1780 (gohome_move_update), 0x004F1480 (chase_move_update), 0x004F0DD0 (blocker_move_update), 0x004F1890
    // (bail_move_update): the move updates the update type names.
    void go_home_update();
    void chase_update();
    void blocker_update();
    void bail_move_update();

    // --- the move updates (the original's vtable slots 13-15: no other class has them, so plain methods) ---
    // 0x004EFBC0 (lip_trick_update): the display matrix turned upright about up_dir_.
    void lip_trick_update();
    // 0x004F2A10 (slot 13, generic_move_update): a walker along its path.
    void generic_move_update();
    // 0x004F2AD0 (slot 14, skater_move_update; not a Ghidra function): a skating ped along its path, its waypoints,
    // grind bails, crouches, spins and sounds.
    void skater_move_update();
    // 0x004ED4A0 (slot 15, no_move_update): the display matrix kept if the motion component is rotating, else put back.
    void no_move_update();

    // --- script member functions (the table at 0x00683730) ---
    bool ped_set_logic_state(script::Struct* params, script::Script* script);           // 0x004F2040
    bool ped_get_logic_state(script::Struct* params, script::Script* script);           // 0x004EDD80
    bool ped_start_moving(script::Struct* params, script::Script* script);              // 0x004F24B0
    bool ped_stop_moving(script::Struct* params, script::Script* script);               // 0x004ED150
    bool ped_is_moving(script::Struct* params, script::Script* script);                 // 0x004ED160 [0x68D298B0]
    bool ped_set_stick_distances(script::Struct* params, script::Script* script);       // 0x004ED170 [0x2CC086DD]
    bool ped_set_is_grinding(script::Struct* params, script::Script* script);           // 0x004ED1D0
    bool ped_get_current_velocity(script::Struct* params, script::Script* script);      // 0x004EDDA0
    bool ped_store_max_velocity(script::Struct* params, script::Script* script);        // 0x004EDDD0
    bool ped_get_original_max_velocity(script::Struct* params, script::Script* script); // 0x004EDE00
    bool ped_apply_speed_boost(script::Struct* params, script::Script* script);         // 0x004EDE20
    bool ped_remove_speed_boost(script::Struct* params, script::Script* script);        // 0x004EDEA0
    bool ped_set_is_bailing(script::Struct* params, script::Script* script);            // 0x004ED220
    bool ped_near_vert_node(script::Struct* params, script::Script* script);            // 0x004ED2A0
    bool ped_set_is_skater(script::Struct* params, script::Script* script);             // 0x004EE100
    bool ped_play_jump_sound(script::Struct* params, script::Script* script);           // 0x004EE180
    bool ped_play_land_sound(script::Struct* params, script::Script* script);           // 0x004EE1B0
    bool ped_get_current_node_names(script::Struct* params, script::Script* script);    // 0x004EE1E0
    bool ped_get_gender(script::Struct* params, script::Script* script);                // 0x004EE230
    bool ped_activate_aux_script(script::Struct* params, script::Script* script);       // 0x004F24E0
    bool ped_deactivate_aux_script(script::Struct* params, script::Script* script);     // 0x004F20F0
    bool ped_hide_board(script::Struct* params, script::Script* script);                // 0x004EE280
    bool ped_get_voice_prefix(script::Struct* params, script::Script* script);          // 0x004EE3A0
    bool ped_bail(script::Struct* params, script::Script* script);                      // 0x004F20E0 [0xEE24A410]
    bool ped_init_path(script::Struct* params, script::Script* script);                 // 0x004F0740
    bool ped_hit_waypoint(script::Struct* params, script::Script* script);              // 0x004F2770
    // 0x004EDEC0 [0xDA7BE9E2]: heading / jumpSpeed / jump_time of a jump of `height` under `gravity` to the next node.
    bool ped_get_jump_to_next_node(script::Struct* params, script::Script* script);
    // 0x004F0950: the motion component looks at 48 ahead along the current node's angles.
    bool ped_turn_to_current_waypoint_orientation(script::Struct* params, script::Script* script);

private:
    uint32_t kind_{};            // +0x18 0 a walker, 2 a skater (Ped_SetIsSkater)
    uint32_t female_{};          // +0x1C 1: SkeletonName is Ped_Female
    uint32_t flag_bits_{0};      // +0x20 PED_FLAG_*
    uint32_t cur_phase_;         // +0x24 the logic state (LogicState)
    uint32_t state_table_;       // +0x28 its table
    uint32_t update_type_;       // +0x2C ped_update_type (the move update run each frame)
    script::Script* aux_script_{nullptr}; // +0x30
    std::array<float, 4> avoid_{};           // +0x34 the steering this frame, per quarter turn (cos-weighted; -1..1)
    std::array<float, 4> bias_{};            // +0x44 the lasting steering bias, per quarter turn
    uint32_t list_link_{};       // +0x54 the node it came from (a name)
    uint32_t next_node_{};       // +0x58 the node it's heading for
    uint32_t node_after_{};      // +0x5C the node after that (chosen ahead)
    maths::Vec4 node_pos_{};     // +0x60 list_link_'s position
    maths::Vec4 next_node_pos_{};  // +0x70
    maths::Vec4 node_after_pos_{}; // +0x80
    int turn_frame_{0};            // +0x90 frames into the turn towards the node after
    int turn_frames_{0};           // +0x94 the turn's length (0: not turning)
    ObjectGroup* peds_{nullptr};   // +0x98 the path manager's extra group (the other peds to avoid)
    float step_seconds_{};       // +0x9C this frame's (seconds)
    maths::Vec4 up_dir_{};       // +0xA0 the up it blends from
    maths::Vec4 up_target_{};    // +0xB0 the up it blends to
    maths::Vec4 up_shown_{};     // +0xC0 the up it has
    float up_blend_{};           // +0xD0 1 .. 0 along the blend
    float stick_above_;          // +0xD4 DistAbove (ped_skater_stick_dist_above)
    float stick_below_;          // +0xD8 DistBelow (ped_skater_stick_dist_below)
    float stored_max_velocity_{0.0f}; // +0xDC Ped_StoreMaxVelocity
    float speed_ratio_{};        // +0xE0 the motion component's speed over its max speed (the looping sound's)
    int spin_angle_{};           // +0xE4 the jump's spin (degrees, signed)
    float twist_rate_{};         // +0xE8 how far it has spun (degrees)
    float spin_time_{};          // +0xEC the jump's length (seconds)
    float spin_elapsed_{};       // +0xF0
    maths::Mat4 jump_matrix_{};  // +0xF4 the display matrix at the take-off
    float vert_rotation_{};      // +0x134 how far it has turned over the spine (degrees)
    float vert_rotation_target_{}; // +0x138
    maths::Mat4 saved_matrix_{};   // +0x13C the display matrix kept
    std::array<maths::Vec4, 5> past_frames_{}; // +0x17C its last positions (the newest last)
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(PedestrianBrainModule, kind_) == 0x18 && offsetof(PedestrianBrainModule, flag_bits_) == 0x20 &&
                          offsetof(PedestrianBrainModule, aux_script_) == 0x30 &&
                          offsetof(PedestrianBrainModule, avoid_) == 0x34 && offsetof(PedestrianBrainModule, bias_) == 0x44 &&
                          offsetof(PedestrianBrainModule, list_link_) == 0x54 &&
                          offsetof(PedestrianBrainModule, node_pos_) == 0x60 &&
                          offsetof(PedestrianBrainModule, turn_frame_) == 0x90 &&
                          offsetof(PedestrianBrainModule, peds_) == 0x98 && offsetof(PedestrianBrainModule, up_dir_) == 0xA0 &&
                          offsetof(PedestrianBrainModule, up_blend_) == 0xD0 &&
                          offsetof(PedestrianBrainModule, spin_angle_) == 0xE4 &&
                          offsetof(PedestrianBrainModule, jump_matrix_) == 0xF4 &&
                          offsetof(PedestrianBrainModule, vert_rotation_) == 0x134 &&
                          offsetof(PedestrianBrainModule, saved_matrix_) == 0x13C &&
                          offsetof(PedestrianBrainModule, past_frames_) == 0x17C && sizeof(PedestrianBrainModule) == 0x1CC,
                      "layout must match the original");
    }
};

// 0x004EE2C0: the current profile's voice: its voice= picks the entry of player_voices with that id (its stream_name;
// null if none), else Male1 / Female1 by is_male.
const char* profile_voice();

} // namespace objects

namespace objects {
// The script member table's records (their code is filled in by engine/script/member_function_code.cpp).
std::array<script::MemberFunctionEntry, 29>& ped_logic_member_functions();
} // namespace objects
