#pragma once

#include <cstdint>
#include <array>
#include <span>

#include "engine/maths/vec4.h"

namespace modules {
struct FrontEnd;
}
namespace objects {
class ControlButton;
class GameEntity;
class Rider;
} // namespace objects
namespace script {
class Array;
class Script;
class Struct;
} // namespace script

// NOT ORIGINAL - THAW's BMX ridden in THUG2 (ours, added at the user's request; not the game's and not PartyMod's):
// phase 2, a rideable bike with the skater's handling.
//
// THAW's bike is a mode of the skater's core physics (a byte at its component's +0x524, set by the member SkaterOnBike)
// with a physics parameter set of its own (bicycle_physics) and the bike a special item. Here:
// - the bike state lives in a side structure keyed by the core physics component (our component's layout is THUG2's;
//   THAW's offsets don't apply), BikeState below;
// - the 17 bike member functions of THAW's core physics (SkaterOnBike ... exitbikeendo) are answered by
//   SkaterPhysicsModule::invoke_method_by_name through invoke_method_by_name below - registered as script member
//   functions only while the feature is on; plus ThawBmxLoadAssets (ours: the bike's skeleton and animations loaded
//   once, permanent);
// - get_physics_structure (0x005802E0, rider.cpp) gives thaw_bicycle_physics (THAW's bicycle_physics, put into the
//   overlay's scripts by tools/pipeline/thaw_bmx/convert.ts) while the skater is on the bike;
// - the ride itself is THAW's own bike scripts (phase 4: bike_scripts.qb / bike_grindscripts.qb / bike_sfx.qb ported
//   by tools/pipeline/thaw_bmx/bike_scripts.ts - bike_start, bike_onground, BIKE_OLLIE, bike_air_state, BIKE_GRIND, bike_manual,
//   the trick lists...), their animation-tree calls (Skater_PlayBikeAnim and THAW's Skater_Playanim family) played with
//   THUG2's PlayAnim on the rider and on the bike (thaw_bmx_details, phase 1); the bike's partials (wheels, bars) as
//   anim subsets - all scripts, in tools/pipeline/thaw_bmx/convert.ts's thaw_bmx.ns. The manual's pose (THAW 0x00620780: the
//   wobble animation's place from the manual angle) is end_frame's (thaw_bmx_tricks.cpp).
// "On the bike" is the bike flag alone (THAW's +0x524; phase 2 and 3 rode as THUG2's vehicle "thaw_bmx" too, phase 4
// dropped that): the skater's reset (Rider::reset_components - a retry, a level change: its special items, the bike,
// destroyed) ends the ride.
//
// Getting on / off: the key [Keybinds] ThawBmx= (an SDL scan code or key name; default F7) in a level, the pause
// menu's RIDE BMX / GET OFF BMX (make_menu_level), or the script thaw_bmx_toggle (the test-automation channel's
// `run thaw_bmx_toggle`). Single player only (the bike state isn't sent to other players).
//
// thug2-decomp.ini [features] thaw_bmx=1 turns it on (default 0: nothing registered, the physics structure, the
// member functions and resets are THUG2's own).
//
// Addresses: "THAW 0x..." (and a bare address in THAW's context) is THAW's reference build's - the THAW PC exe the BMX
// work reads (tools/pipeline/thaw_bmx/thaw_ghidra.py); THUG2's are the reference build's, as everywhere.
namespace skater {
class SkaterPhysicsModule;
} // namespace skater

namespace skater::thaw_bmx {
constexpr float kDefaultSteerBlend = 0.5F;

// THAW's wheel points sit this far ahead of / behind the skater's position along its facing (THAW 0x006DD14C,
// loaded by 0x005DF050).
constexpr float kWheelOffset = 24.0F;
// THAW's ResetBikeManualAngle value (0x006DD6C8, loaded by 0x005C7510).
constexpr float kManualAngleReset = 50.0F;
// THAW's BikeExitSkid sets the skid's timer (+0x554) to this.
constexpr int kSkidTimerReset = 60;

// A digital direction of the movement stick as THAW's pad has them (its LEFTUP / LEFTDOWN / LEFTLEFT / LEFTRIGHT
// buttons, pad +0x280..+0x2E0 - THUG2's pad has no such buttons: thaw_bmx_physics.cpp makes them from the d-pad and
// the stick): held, and the game clock (with its offset) when it went down.
struct StickButton {
    bool down = false;
    int press_time_ = 0;
};

// THAW's bike state on its core physics component (offsets of THAW's component in the comments).
struct BikeState {
    bool on = false;             // +0x524 SkaterOnBike / SkaterOffBike
    bool manual = false;         // +0x525 the bike's manual (the manual pitch, manual_pitch, sets it)
    bool nose_manual_ = false;   // +0x526
    bool xup = false;            // +0x527 BikeEnterXup / BikeExitXup
    bool front_brake_ = false;   // +0x528 the brake button is the front one (L1; R1 the rear)
    bool tire_tap_nose_ = false; // +0x529 (the tire tap began with up held: on the front wheel)
    bool tire_tap_ = false;      // (THAW: its timed flag +0x90)
    bool tap_armed_ = false;     // +0x54C in vert: the tire tap looked for every frame (once asked), buttons or not
    bool tap_ledge_ = false;     // +0x54D the tap point is a ledge's (a rail's), not a surface below
    float tap_pitch_ = 0.0F;     // (ours) how far the bike has turned over for the tire tap so far (radians)
    bool landed_from_tire_tap_ = false;                 // +0x70D (THAW: +0x54E copied at the landing)
    maths::Vec4 tap_point_{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F}; // +0x52C where the wheel taps
    maths::Vec4 tap_start_{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F}; // +0x53C where the bike was then
    bool skid = false;                                  // +0x550 BikeEnterSkid / BikeExitSkid
    bool can_skid_ = true;                              // +0x558 (THAW's constructor sets it)
    int skid_timer_ = 0;                                // +0x554
    bool endo = false;                                  // +0x4D0 / +0x4E4 (exitbikeendo clears them)
    maths::Vec4 endo_velocity_{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F}; // +0x4D4 the velocity the endo began with
    float manual_angle_ = kManualAngleReset;            // +0x56C
    int no_balance_frames_ = 0;                         // +0x18CC frames the manual pitch waits after a manual starts
    bool over_grind_ = false;                           // +0x18C8
    float air_pitch_ = 0.0F;                            // +0x66C the pitch in the air so far (degrees; 0 on the ground)
    float last_air_pitch_ = 0.0F; // +0x18C0 air_pitch_ as the last air frame began (PitchingForward / PitchingBackward)
    maths::Vec4 front_wheel_{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};      // +0x18D0
    maths::Vec4 rear_wheel_{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};       // +0x18E0
    maths::Vec4 last_front_wheel_{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F}; // +0x18F0 (at the frame's start)
    maths::Vec4 last_rear_wheel_{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};  // +0x1900
    StickButton stick_up_, stick_down_, stick_left_, stick_right_; // (THAW's pad +0x280 / +0x2A0 / +0x2C0 / +0x2E0)
    bool vert_ground_noted_ = false; // (ours: THAW's constructor 0x005CF6A0 sets +0x6F8 - never on vert ground yet)
    int vert_ground_time_ = 0;       // +0x6F8 the game clock (with its offset) last on vert ground
    float steer_blend_ = kDefaultSteerBlend;       // +0x570 the turn animation's share of the turn branch (THAW 0x005D61F0), 0.5 .. 1
    bool steer_anim_ = false;        // (ours) bike_playturnanim's turn animation on (the member ThawBmxSteerAnim)
    float steer_in_ = 0.0F;          // (ours) how far THAW's tree has blended the turn branch in (0 .. 1)
    bool stall_script_ = false;      // +0x49D ShouldRunStallScript (R1 held as the grind started)
    maths::Vec4 stall_velocity_{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F}; // +0x4A0 the velocity the grind began with
    int stall_steep_frames_ = 0; // +0x724 frames stalled on a steep rail (never reset but by the constructor)
    // (THAW's trick component, thaw_bmx_sticks.cpp) each stick's exclusive direction (0 none, 1-8: left, right) and
    // +0x1C14, the right stick centred since BikePress last counted (1 after a reset)
    std::array<int, 2> trick_stick_ = {{0, 0}};
    bool right_stick_centred_ = true;
};

// [features] thaw_bmx (default 0; the self-tests turn it on for a moment).
bool& is_switched_on();
// The bike state of `core` (a core physics component); null when it has none and `create` is false.
BikeState* state_of(const SkaterPhysicsModule* core, bool create);
// Its state forgotten (a component made at the same address starts off the bike).
void get_forget(const SkaterPhysicsModule* core);
// Whether `core`'s skater rides the bike: the bike flag (SkaterOnBike).
bool on_bike(const SkaterPhysicsModule* core);
// The physics' gate (thaw_bmx_physics.cpp): the feature on and the skater on the bike. Off: the skater's physics,
// unchanged.
inline bool live_now(const SkaterPhysicsModule* core) { return is_switched_on() && on_bike(core); }
// Whether the keyboard's half-stick key ([Keybinds] Feeble) counts (sdl_input.cpp): the feature on and a skater on
// the bike. THAW's PC keyboard has it (its key binding k0_24 halves the movement keys' push; PartyMod's THAW build
// keeps it as Feeble, "Half Stick") - so its Smith / Feeble grinds (the stick a little up / down) can be had on keys.
// THUG2's PartyMod reads the binding but never uses it: off the bike the keys push the stick all the way, as before.
bool half_stick_key();
// The script a skater is sent to by MakeSkaterGoto (game_flow_cfuncs.cpp) - `name` itself unless THAW's general skater
// scripts take their bike branch there. On the bike (the feature on), THAW's PedKnockDown (tricks.qb): from a ped's
// knock-down state (THUG2's STATECONTROL_knockdown - THAW's STATESCRIPT_knockdown, which sends a rider's ped to its
// disgust state instead) none - 0, the skater not sent; from anywhere else (a chasing ped's catch, a blocker's bump)
// the bike's own, bike_PedKnockDown (the bike's bail), when the overlay has it. `caller` the calling script (or null).
uint32_t skater_script(const objects::Rider* skater, uint32_t name, const script::Script* caller);
// The script run and the one calling it.
struct ScriptNames {
    uint32_t name{};
    uint32_t caller_name_{};
};

// Its choice: `caller_name` the calling script's top-level name (0: none), `bike_script_loaded` whether the overlay has
// bike_PedKnockDown.
uint32_t bike_skater_script(ScriptNames script_names, bool bike_script_loaded);
// The physics structure while on the bike (thaw_bicycle_physics; null off the bike or when the overlay hasn't it).
script::Struct* physics_structure(const SkaterPhysicsModule* core);
// A bike member function on `core` (1 / 0 its result), or 2: not one of ours.
int invoke_method_by_name(SkaterPhysicsModule* core, uint32_t name, script::Struct* params, script::Script* script);
// The wheel points from the skater's position and facing (THAW 0x005C98E0's second half).
void set_wheel_points(BikeState& state, const maths::Vec4& position, const maths::Vec4& facing);

// At start-up, after the game's own: ThawBmx_Available (a C function, always: false while the feature is off),
// ThawBmx_LoadTexture (always: ingest_skin unless that texture is loaded - the bike manual meter's pictures) and,
// while it's on, the bike's member functions.
void register_script_functions();
// From Rider::reset_components (its special items destroyed - the bike with them): off the bike, out of the vehicle,
// the board and the camera back (the script thaw_bmx_after_reset).
void on_skater_reset(objects::Rider* skater);
// Once a frame from the front end's input logic (mouse_menus::tick_frame): the key.
void tick_frame(modules::FrontEnd* front_end);

// ---------------------------------------------------------------------------------------------------------------
// Phase 3 (thaw_bmx_physics.cpp): THAW's bike branches in the skater's core physics, re-expressed on THUG2's code.
// Each is called from our THUG2 function at the place THAW's branch sits, only while live_now(core) - otherwise the
// THUG2 function runs exactly as before. THAW's addresses are in each function's comment.

// At the start / end of the core physics' update (0x00510830; THAW 0x005E8640): the movement stick's directions as
// THAW's pad buttons, the wheel points kept (THAW +0x18F0 / +0x1900) / the x-up let go (THAW 0x005C9210).
void begin_frame(SkaterPhysicsModule* core);
void end_frame(SkaterPhysicsModule* core);

// On the ground (do_ground_physics 0x0050EE70; THAW 0x005E4200..0x005E4DBF):
// THAW 0x005E4200: the air pitch back to 0, the tire tap over.
void get_on_ground(SkaterPhysicsModule* core);
// THAW 0x005E4790: whether the ground's gravity pulls the bike along this frame - not when it's slower than 10 and
// the gravity points back along it, unless in a manual without a brake held (the bike doesn't roll back).
bool ground_gravity_applies(SkaterPhysicsModule* core, const maths::Vec4& gravity);
// THAW 0x005CAB60 (the brake check, on the bike): L1 the front brake, R1 the rear, when the skater may brake (+0x311).
bool should_brake(SkaterPhysicsModule* core);
// THAW 0x005CACA0 (Brake, on the bike): not the rear brake in a nose manual nor the front one in a manual; the skid
// brake out of a manual, the front brake in an endo.
void get_brake(SkaterPhysicsModule* core);
// THAW 0x005CAEF0 (whether to pedal, on the bike): not in a manual, rolling backwards or with a brake held.
bool can_kick(SkaterPhysicsModule* core);
// THAW 0x005CB050 (the pedalling's push, on the bike): not rolling backwards or in a manual.
bool may_accelerate(SkaterPhysicsModule* core);
// THAW 0x005D61F0 (the bike's own turning on the ground, THAW 0x005DA720 calls it on the bike): by the stick, sharper
// slower and with a brake held; rolling backwards the other way round (steered by the manual's balance outside one);
// the skid's timer; the wheel points turned along.
void turn_on_ground(SkaterPhysicsModule* core);
// THAW 0x005CDA50: the manual pitch (stick back: a manual, forward: a nose manual, the front brake fast: an endo).
void manual_pitch(SkaterPhysicsModule* core);

// Jumping (perform_leap 0x00509450; THAW 0x005DDDC0): from a standstill a boost along the facing; out of a manual the
// skater pitched by the manual's angle.
void jump_boost(SkaterPhysicsModule* core);
void jump_pitch(SkaterPhysicsModule* core);

// In the air (do_air_physics 0x0050CA00; THAW 0x005DF880 calls 0x005D7CB0 on the bike instead of its lean and spin):
// no manual in the air; the bike's pitch and spin by the stick (the fast ones with L1), back upright when let go, a
// bail when the rider's head hits something.
void on_air(SkaterPhysicsModule* core);
void air_rotation(SkaterPhysicsModule* core);

// The ground under the bike (check_for_ground 0x00509BD0; THAW 0x005DF050 on the bike, out of a manual): the two wheels
// felt for the ground (THAW 0x005D9650) - kWheelsOff: the leading one gone, off the ground; kWheelsOnGround /
// kWheelsOnGroundNoHit: the skater midway between them, the ground normal across the wheel base (the feeler under the
// skater hit or not); kWheelsNotUsed: THUG2's own check (in a manual, or [features] thaw_bmx_wheels=0).
// [debug] thaw_bmx_wheels=1 logs each frame the wheels leave the ground (`thaw_bmx wheels off:` - each wheel's hit,
// why not, distance, reach, turn).
enum class WheelGround : uint8_t { kWheelsNotUsed, kWheelsOff, kWheelsOnGround, kWheelsOnGroundNoHit };
WheelGround wheel_ground_check(SkaterPhysicsModule* core);
// Whether the skater rolls backwards on the bike (the velocity against the facing) - THAW's ground check (0x005DEB08)
// turns the facing round for its steepness test then.
bool rolling_backwards(SkaterPhysicsModule* core);
// THAW 0x005CB420 (the display normal's lerp, on the bike on the ground): towards the ground normal across the
// wheel base, at Normal_Lerp_Speed_Bike. Returns false when THUG2's own (0x004FEA50) is to run.
bool lerp_display_normal(SkaterPhysicsModule* core);

// THAW 0x005CF2F0 (the grind trick, on the bike): the over-grind state set, and BikeGrindTrickList's row for how far
// the stick is pushed up or down with `*column` its element - null when the overlay hasn't the list (THUG2's
// GrindTrickList then). `left_side`, `sideways`, `backwards` as start_grind_trick's first three.
script::Array* grind_trick_row(SkaterPhysicsModule* core, bool left_side, bool sideways, bool backwards,
                               uint32_t* column);
// THAW 0x005E27F5: a bad ledge on the bike's side: the grind on the other side.
bool grind_side(SkaterPhysicsModule* core, bool left_side, bool bad_ledge_on_that_side);
// THAW 0x005D1890: the acid drop's search direction on the bike - its facing, flattened; the other way with the stick
// pulled back. False: not on the bike.
bool acid_drop_direction(SkaterPhysicsModule* core, maths::Vec4* direction);

// THAW 0x005CA3B0, the member EnterWheelieFromAir (the bike's manual from a landing; `nose` its parameter).
int enter_wheelie_from_air(SkaterPhysicsModule* core, script::Struct* params);

// ---------------------------------------------------------------------------------------------------------------
// Phase 4 (thaw_bmx_tricks.cpp): what THUG2's vehicle mode used to hide - each THAW's own bike gate -, the tire tap,
// the manual's pose, the bike's sounds and the THAW-only members its bike scripts ask. Called only while live_now(core)
// (the sounds: while the skater is on the bike); otherwise THUG2's code runs unchanged.

// THAW 0x005D6FCF (the wallride's start): on the bike square held starts a wallride too (THUG2's: the triangle).
bool wallride_button(SkaterPhysicsModule* core);
// THAW 0x005DB1ED (the wallplant): on the bike square held plants without asking the trick list.
bool wallplant_by_square(SkaterPhysicsModule* core);
// ... else the bike's list, bike_Wallplant_Trick (THAW's; THUG2's Wallplant_Trick when the overlay hasn't it).
uint32_t wallplant_trick_list();
// THAW 0x005D3330: on the bike skitching is R2 held long enough (the skater's: up).
objects::ControlButton* tow_button(SkaterPhysicsModule* core);
// THAW 0x005DF98B (in the air, after the recovery): the tire tap - looked for (0x005DC290: R1 and up / down held,
// a ledge ahead: THUG2's rail search with the hang check, ledge_point below) or carried on (0x005CE920: the wheel to the
// tap point).
void tire_tap(SkaterPhysicsModule* core);
// Whether a tire tap is on (THAW's timed flag +0x90; 0x005DF9DB: the air's keep-upright skipped then).
bool tire_tapping(const SkaterPhysicsModule* core);
// THAW 0x00620780: the manual's pose - the wobble animation (bike_manual's bmx_wheelie_init / nosewheelie ones played
// with `wobble`) held at the manual angle x 0.01 of its length, on the rider and the bike (end_frame calls it); out of
// a manual, the running balance trick's balance (a grind's, a stall's range animation) on both the same way.
void manual_pose(SkaterPhysicsModule* core);

// The bike's sounds (THAW's skater sound component 0x005F197B / 0x005F1A10 / 0x005F1AB0 and its looping sound
// 0x0046A097 / 0x0046A0D3: on the bike BIKE_LAND, BIKE_GRINDLAND, BIKE_OLLIE, BIKE_GRINDJUMP, BIKE_BONK, BIKE_ROLL,
// BIKE_GRIND - TERRAIN_BIKE_DEFAULT_*, every terrain the same; nothing in the air), from THAW's tables copied into the
// overlay (thaw_bmx_terrain_land ...).
enum class BikeSound : uint8_t { kLand, kGrindLand, kOllie, kGrindJump, kBonk, kRoll, kGrind, kAir };
// For the skater object `skater`: -1 not on the bike (THUG2's terrain sound), 0 no sound, 1 the sound in `info`
// (a surface_kind::SoundInfo).
int bike_sound_of(const void* skater, BikeSound kind, void* info);

// The bike drawn into the rider's shadow (render::set_shadow_companion, registered while the feature is on): for the
// local skater's model on the bike, the bike's model while it's shown; else null.
void* shadow_companion(void* caster_model);

// The flip on the bike, as THAW's. THAW's Flip (0x005E97D0) and its "after the animation" flags (0x005E9960,
// FlipAfter) only turn the skater's flipped flag (its state component's +0x28); the animation tree takes the flag when
// a branch is played (Skater_Playanim: HandleFlipOrBoardRotateAfter, then obj_is_flipped - the bike's, played with
// not_a_skater, from skater:flipped) and the branches already playing stay as they were. THUG2's Flip mirrors every
// running animation at once (RiderSpinAndFlip::apply_flip_to_anims), and the bike followed a step behind
// by its own script toggling its channels (thaw_bmx_bike_flip's Obj_Flip - not the way the rider's are set). THAW's
// bike scripts count on THAW's way: BIKE_GRIND flips for a grind on the other side and its loop's orient_vehicle
// turns the flag back every frame (the grind stays mirrored in THAW; THUG2's way un-mirrored rider and bike as the
// loop began), a left tailwhip (flip_anim) flips at its start and twice more at its end (FlipAfter, OnExitRun
// bike_ExitGrabTrick_flip - THUG2's way mirrored the finished pose and back), the backwards revert flips around its
// pivot. Here the rider's and the bike's channels both take the flag as THAW's branches do, by the same call.
// Whether `core`'s Flip only turns the flag: the feature on and the skater on the bike.
inline bool flip_flag_only(const SkaterPhysicsModule* core) { return live_now(core); }
// The flip a PlayAnim / Obj_PlayAnim on `object` gives its channel before it's played (AnimBlenderComponent::
// pass_to_channel): for the local rider on the bike (what was left for after the animation done first - THAW's
// HandleFlipOrBoardRotateAfter) and for his bike, his flipped flag: 1 / 0; else -1 (THUG2's: the channel's as it was).
int play_flip(objects::GameEntity* object);
// Off the bike (SkaterOffBike, the skater's reset on it): `core`'s rider's animations mirrored as his flag says
// (apply_flip_to_anims) - THUG2's way from then on.
void flip_anims_to_flag(SkaterPhysicsModule* core);

// The THAW-only members of the bike scripts besides the bike's own (invoke_method_by_name answers them; 2: not one):
// DisplayPitchGreaterThan (0x005CA490), LandedFromTireTap (0x005C7AF0), InTireTap (its state component's +0x90),
// PitchingForward / PitchingBackward (its matrix queries), ShouldRunStallScript / ClearStallScriptFlag /
// ExitStallBoost (0x005C8410 / 0x005C8420 / 0x005C83D0 - the stall below), OnStall.
int call_trick_member(SkaterPhysicsModule* core, uint32_t name, script::Struct* params, script::Script* script);

// ---------------------------------------------------------------------------------------------------------------
// Phase 6 (thaw_bmx_physics.cpp): steps, crests and vert.

// THAW 0x005C2820 (the adjust-physics component's ground-below check, the "uber frig", on the bike): its feeler starts
// Skater_First_Forward_Collision_Height higher - the bike's position, midway between its wheels, can sit below the
// ground's surface on a step or over a crest (the wheels on the ground, the line between them through it). Ours also
// raises the "under the floor" check's end by as much (THAW doesn't - phase 6: the
// same midway position under a step's top would turn the bike back there). 0: not on the bike.
float stuck_check_raise(SkaterPhysicsModule* core);
// THAW 0x005CA8F0 (clamp_skater_speed, THAW's for every skater - here on the bike only, the keys being bicycle_physics'):
// for Skater_Vert_Max_Speed_Time after the bike was last on vert ground (THAW's ground update 0x005E4579 notes the
// time while its feeler hits vert) the max-max speed is Skater_Vert_Max_Max_Speed_Stat and the heavy air friction
// over the max speed isn't applied. `*max_max` changed; returns whether the heavy friction may apply this frame.
bool vert_speed_allowance(SkaterPhysicsModule* core, float* max_max);
// THAW 0x005E4579: on the ground, the time noted while the ground feeler's face is vert.
void note_vert_ground(SkaterPhysicsModule* core);

// ---------------------------------------------------------------------------------------------------------------
// Phase 7 (thaw_bmx_physics.cpp, thaw_bmx_tricks.cpp): the bike branches phases 2-6 couldn't read, read whole.

// THAW 0x005E0476: in the air, once the display matrix is the skater's again, the wheel points follow the skater.
void air_wheel_points(SkaterPhysicsModule* core);
// THAW 0x005DA210 (break vert, on the bike): the movement stick's up held longer than `push_time` ms (neither left nor
// right, square nor circle) - THUG2's d-pad up hold.
bool vert_push_held(SkaterPhysicsModule* core, int push_time);
// ... and R1 with the stick up or down, or a tire tap looked for in vert: the spine transfer / push-off path.
bool vert_tap_held(SkaterPhysicsModule* core);
// (The manual meter is THAW's own, drawn by its bike scripts - create_bike_manual_meter / update_bike_manual_meter in
// bike_manual_state: its pictures turned by 0.9 degrees per point of the manual angle, GetBikeManualAngle. The DLL
// only loads its pictures: the C function ThawBmx_LoadTexture, thaw_bmx.cpp.)

// ---------------------------------------------------------------------------------------------------------------
// Riding backwards and the turn's lean (thaw_bmx_physics.cpp). Riding backwards: THAW's 0x005CCDD0 (THUG2's
// 0x004FF920, the turn round to fakie) does nothing on the bike - do_ground_physics skips it; the bike stays in THAW's
// backwards balance (bike_backwards_balance_trick) until it stops, loses the balance or R2 pivots it round.

// The bike's own object (the special item ThawBmx on `skater`), or null.
objects::GameEntity* bike_object_of(void* skater);
// THAW's turn branch (bike_playturnanim's Skater_PlayTurnAnim: a TurnBlend of <anim> and <turn_anim>, its share the
// physics' +0x570 - THAW 0x0061FAC0 / 0x00620080) on THUG2's animation: bike_playturnanim plays the turn
// animation in the anim subset ThawBmxTurn on the rider and the bike (thaw_bmx_playturnanim) and asks for it with the
// member ThawBmxSteerAnim (on / off: off from its exit script); here, at the end of each frame, the subset's weight is
// set to THAW's share x how far its tree would have blended the branch in (0.4 s) or out (kSteerOut) - and the subset
// removed once it's out. end_frame calls it.
void steer_anim(SkaterPhysicsModule* core);
// The member ThawBmxSteerAnim (ours): `on` / `off`.
void set_steer_anim(SkaterPhysicsModule* core, bool on);
// The turn animation's subset gone at once (SkaterOffBike, on_skater_reset - before the state goes).
void steer_anim_off(SkaterPhysicsModule* core);

// ---------------------------------------------------------------------------------------------------------------
// The stall (thaw_bmx_physics.cpp; core_rail.cpp SkaterPhysicsModule::do_bike_stall_physics): R1 held as a grind starts
// on the bike. THAW's own physics state 6 (core_fields::kBikeStall): the bike still on the rail, facing along it, out
// by a jump (BikeStallTrick's Ollied: StallOllieOut) or, on a rail too steep, sliding off into a grind.

// THAW 0x005E2935 (the grind start, after do_extra_grind_trick, before the grind trick): R1 held -
// ShouldRunStallScript set, the velocity kept, and the bike moving across the rail (`start_velocity`, the velocity the
// grind start began with, flattened, within Rail_Tolerance of square to `direction`) straight into the stall state;
// else the flag cleared.
void grind_stall(SkaterPhysicsModule* core, const maths::Vec4& start_velocity, const maths::Vec4& direction);
// THAW 0x005E7074 (the rail state's update, at its end): with the flag set, still on the rail - the grind
// slows by the kept velocity / stall_slip_time a frame, and stall_slip_time ms into it stalls; off the rail - the flag
// cleared.
void rail_stall_slip(SkaterPhysicsModule* core);
// THAW 0x005E3801 (the stall state's update, at its end): on a rail steeper than the up's y 0.95 for more
// than 10 frames (counted over every stall) back to grinding it. True when it did.
bool stall_steep_rail(SkaterPhysicsModule* core);

// ---------------------------------------------------------------------------------------------------------------
// THAW's stick buttons, the right stick's tricks and the d-pad camera (thaw_bmx_sticks.cpp). On the bike THAW turns
// the input server's "left stick drives the d-pad" off (bike_start's SetAnalogStickActiveForMenus 0) and the d-pad
// turns the camera (skatercam0:LookaroundOnDPad_On); its bike reads the sticks through buttons of their own instead:
// - its pad has eight buttons more than THUG2's (pad +0x200..+0x2E0): each stick's four directions, made by its input
//   server from the tolerance-centred stick bytes - the movement (left) stick any way off its centre (0x4355B0: past
//   THUG2's own dead band, 50 from 0x80), the right stick past 0xCB / under 0x35 (0x435530: about 60% over) - and
//   updated as buttons (0x596CA0: down while the byte is set). Here every pad keeps them beside it while the feature
//   is on (PadSnapshot::tick_frame / read_button), under THAW's names (leftup ... rightright: DoBalanceTrick
//   ButtonA=leftright, HeldLongerThan button=leftdown);
// - its trick component has each stick as eight exclusive directions (0x471D20: d_pad_direction over the four, as
//   THUG2's d-pad: leftup, leftdown, leftleft, leftright, leftupleft, leftupright, leftdownleft, leftdownright and the
//   right stick's) - press / release events in the trick buffer, `Held` / `Released` - here only while the skater
//   rides the bike (update_buttons, is_key_held, Held buttons=), so off it THUG2's buffer is as before;
// - its check_trigger has BikePress (0x470EAB): Press, but not the same right-stick flick twice - when the button map's
//   first button (the last trick's) is this one, only after the right stick has been back in the middle (0x4723D0:
//   none of its eight directions down).
using StickButtonIndex = uint8_t;
constexpr int kLeftUp = 0;
constexpr int kLeftDown = 1;
constexpr int kLeftLeft = 2;
constexpr int kLeftRight = 3;
constexpr int kRightUp = 4;
constexpr int kRightDown = 5;
constexpr int kRightLeft = 6;
constexpr int kRightRight = 7;
// The eight directions (a bit each, 1 << StickButtonIndex) from a pad's data: its bytes 0-3, the tolerance-centred
// right x, y and left x, y (0x80 the middle).
uint32_t stick_directions(std::span<const uint8_t> data);
// THAW's names of the pad's stick buttons (by StickButtonIndex).
uint32_t stick_button_name(int index);
// From PadSnapshot::tick_frame / Reset (the feature on): the pad's stick buttons from `data` (null: all up).
void update_pad_sticks(const void* pad, std::span<const uint8_t> data);
// From PadSnapshot::read_button (the feature on): the pad's stick button named `name`, or null.
void* pad_stick_button(const void* pad, uint32_t name);
// The trick component's direction (0 none, 1-8 as d_pad_direction) of one stick's four buttons, and its name (0 for 0).
int stick_direction(uint32_t directions, bool right);
uint32_t trick_stick_name(bool right, int direction);
// From MoveExecutor::update_buttons (the feature on): `trick`'s stick directions as events while it rides the bike.
void update_trick_sticks(void* trick);
// Whether `trick`'s stick direction `name` is down: 1 / 0, or -1 when `name` isn't one or the skater isn't on the bike.
int trick_stick_held(const void* trick, uint32_t name);
// From the end of MoveExecutor::tick_frame (the feature on; THAW 0x4723D0): whether the right stick is centred, for
// the next frame's BikePress.
void end_trick_update(void* trick);
// BikePress's re-arm rule: a press found may count unless the last trick's first button is this one and the right
// stick hasn't been centred since.
bool bike_press_may_count(uint32_t last_button, uint32_t button, bool right_stick_centred);
// Whether `trick`'s right stick has been centred (true when it has no bike state).
bool get_right_stick_centred(const void* trick);
// The look-around's d-pad mode (THAW 0x5B08F0 with +0x40 set): the tilt and heading inputs from the d-pad - up 1.5,
// down -1.0; left -1.5, right 1.5 (half a full stick's x 3) - each x -1 when inverted (THAW's +0x41, its
// default_user_axis_inversion: on by default); the update then eases to them as to the stick's.
struct LookInputs {
    float tilt, heading;
};
LookInputs d_pad_look_inputs(bool up, bool down, bool left, bool right, bool invert);
// THAW's look-around inversion default (default_user_axis_inversion 1; THUG2's stick path has none).
constexpr bool kLookInvertDefault = true;
// From the look-around's LookaroundOnDPad_On / _Off: noted, so that leaving for the front end with the bike's d-pad
// camera on gives the menus the stick back (Update).
void note_d_pad_camera(bool on);
bool d_pad_camera_noted();

// The maths of the branches, on plain values (thaw_bmx_physics.cpp; the self-tests check them against THAW's code as
// read).
namespace bike {
// Phase 7. THAW 0x005D4EF0: one wheel's ground - the feeler's hit (`point`, `normal`,
// `skatable`) under `wheel` (moved since `last`, the frame's start), the skater's `up`, `facing` (`backwards`: rolling
// backwards), the ground normal, Ground_stick_angle(_forward) and Physics_Ground_Snap_Down.
struct WheelFeel {
    maths::Vec4 wheel, last, up, point, normal, ground_normal_, facing;
    bool backwards, skatable;
    float stick_angle_, snap_down_;
};
struct WheelFeelResult {
    bool ground;      // the wheel is on the ground
    bool move_wheel_; // `wheel` is its new place (the hit point plus its normal)
    maths::Vec4 wheel;
    // (for [debug] thaw_bmx_wheels=1's log: why not - 1 not skatable, 2 turns away too steeply, 3 out of reach)
    int why;
    float distance, reach, turn, limit;
};
WheelFeelResult feel_wheel_on(const WheelFeel& in);
// THAW 0x005D61F0: the turn animation's share (+0x570) for the frame's turn rate (radians a
// second before the frame length, ground_turn_rate's) - (|rate / Physics_Ground_Sharp_Rotation| + 1) / 2: half the turn
// pose at the gentlest turn, all of it at the sharpest.
float steer_blend(float rate, float sharp_rotation);
// (ours) THAW's anim tree blending the turn branch in over its BlendPeriod 0.4 while `on`, out over kSteerOut once
// not: `in` (0 .. 1) a frame of `frameLength` seconds on.
float steer_in(float in, bool on, float frame_length);
// THAW 0x005CACA0: which brake acceleration (0 none: the brake is skipped, 1 Physics_Brake_Acceleration, 2
// Physics_Front_Brake_Acceleration, 3 Physics_Brake_Skid_Acceleration).
int brake_choice(bool manual, bool nose, bool front_brake, bool endo, bool balance_is_manual);
// What the ground turn rate is found from: the speeds, the rotations, braking, a manual, the pad, the hold time, riding backwards and the lean.
struct TurnInput {
    float speed{};
    float max_speed_{};
    float rotation{};
    float sharp_rotation_{};
    bool braking{};
    bool manual{};
    float pad_axis_x_{};
    int held_ms_{};
    bool backwards{};
    float balance_lean_{};
};

// THAW 0x005D61F0: the turn rate (radians a second; + left) before the frame length.
float ground_turn_rate(const TurnInput& turn_input);
// The manual pitch's input and what it did (THAW 0x005CDA50).
struct ManualInput {
    bool manual, nose, endo;    // the state before
    float angle;                // the manual angle (+0x56C)
    int no_balance_frames_;     // +0x18CC
    bool up, down, left, right; // the stick's directions (THAW's LEFTUP ...)
    int up_held_, down_held_;   // how long up / down has been held (ms)
    bool front_brake_;          // L1 held
    float stick_y_;             // the stick's analog y (THAW's pad +0x35C, -1 up .. 1 down)
    float speed;                // the velocity's length
    bool forward;               // the velocity along the facing (> 0)
    float no_lean_time_;        // Physics_Ground_No_Lean_Time
    int no_balance_start_;      // Physics_manual_no_balance_frames
    float lean;                 // Physics_Ground_Lean_stat
    float frame_length_;
};
enum class ManualEvent : uint8_t { kNone, kEnterManual, kEnterNoseManual, kEnterEndo, kBail, kLanded, kExitEndo };
struct ManualOutput {
    bool manual, nose, endo;
    float angle;
    int no_balance_frames_;
    ManualEvent event;
};
ManualOutput manual_step(const ManualInput& in);
// THAW 0x005DDDC0: whether a jump gets the standstill boost (no horizontal speed at all, the stick not pulled back).
bool jump_boosts(const maths::Vec4& velocity, float stick_y);
// THAW 0x005D7CB0: the air pitch's settling back towards upright (degrees to add to the pitch so far) once let go.
float settle_air_pitch(float pitch);
// THAW 0x005CF2F0: BikeGrindTrickList's row for the stick's y (-128 up .. 127 down): 0 centred, 1 / 2 a little /
// far up, 3 / 4 a little / far down. THAW reads the movement stick's raw y alone (pad +0x34C; the d-pad isn't looked
// at): on PC the movement keys push it all the way, and THAW's half-stick key (its own k0_24 binding, PartyMod's
// Feeble) half way - half_stick_key below.
int grind_row(float stick_y);
// ... and the column (bit 0 the side, bit 1 sideways) and whether it's an over-grind.
int grind_column(bool left_side, bool sideways, bool backwards, bool* over_grind);
// Phase 6. THAW 0x005CA8F0: `since` ms after the last vert ground, with Skater_Vert_Max_Speed_Time `window` - `*raise`:
// the vert max-max speed; returns whether the heavy air friction over the max speed may apply.
bool vert_window(float since, float window, bool* raise);
// THAW 0x005CB420: the ground normal with its part along the wheel base taken out (normalised).
maths::Vec4 wheel_base_normal(const maths::Vec4& normal, const maths::Vec4& front, const maths::Vec4& rear);
// THAW 0x005DF3E0: the up across the wheel base (right x the base's direction, normalised), the way nearer `hit`.
maths::Vec4 wheel_base_up(const maths::Vec4& right, const maths::Vec4& front, const maths::Vec4& rear,
                          const maths::Vec4& hit);
// Phase 4. THAW 0x005CE920: the tire tap's slowing each frame - the horizontal speed down by 50 (at least 0; THAW
// takes 50 off x and z each and clamps each at 0, which stops a bike going towards -x or -z dead: ours the length,
// as meant), and rising: by a tenth over 100, else by 15.
maths::Vec4 tire_tap_velocity(const maths::Vec4& velocity);
// THAW 0x005CE920: where the bike goes towards the tap point this frame (ours: the result's position; `ends` set when
// the tap is over - the point above the bike while still far off; `lands` when it's reached the point above it).
struct TapStep {
    maths::Vec4 position;
    bool ends, lands;
};
TapStep tire_tap_step(const maths::Vec4& position, const maths::Vec4& tap);
// shadow_companion's rule: `bike_model` when the caster is the rider's model and the bike's is shown (its flags 0x2 and
// not 0x4).
void* shadow_companion_of(const void* caster_model, const void* rider_model, void* bike_model,
                          uint8_t bike_model_flags);
// THAW 0x00620780: the manual's wobble fraction (the manual angle x 0.01).
float manual_wobble(float manual_angle);
// THAW 0x00620780 (its 0x006207A8), not in a manual: the balance trick component's balance for the running kind
// (RiderTeeterMoves::at_hand), and its wobble fraction
// ((4096 + lean) / 8192, flipped (4096 - lean) / 8192 - the rider's balance meter's own).
float balance_wobble(float lean, bool flip);
// THAW 0x005CA490: whether the angle between the model's display up and the ground normal is beyond `degrees`.
bool display_pitch_beyond(const maths::Vec4& up, const maths::Vec4& normal, float degrees);
// The tire tap's ledge (THAW 0x0058C850's search): the nearest
// point on a rail segment from `a` to `b` to the line `start`-`end` (within `margin`; the part along `direction`
// counting a third), not above the line and at most 12 below it - as THUG2's own ledge search (the walk component's
// hang check, which THAW passes it) takes rails in the air. False when it's out of reach. `score`: how far (squared).
bool ledge_point(const maths::Vec4& start, const maths::Vec4& end, const maths::Vec4& direction, const maths::Vec4& a,
                 const maths::Vec4& b, float margin, maths::Vec4* point, float* score);
// A stall test: the velocity at the start, the rail's direction and the tolerance.
struct StallTest {
    maths::Vec4 start_velocity_{};
    maths::Vec4 direction{};
    float tolerance{};
};

// The stall. THAW 0x005E2972: whether the grind starts straight into the stall - `start_velocity` flattened
// and normalised (left alone at length 0) square to `direction` within `tolerance` (Rail_Tolerance > |dot|).
bool stall_across(const StallTest& stall_test);
// THAW 0x005E7082: a frame of the grind with the stall flag set, `elapsed` ms since the state changed:
// `stall` past `slip_time` (stall_slip_time) - else `velocity` less `kept` / `slip_time`.
struct StallSlipStep {
    bool stall;
    maths::Vec4 velocity;
};
StallSlipStep stall_slip(uint32_t elapsed, float slip_time, const maths::Vec4& velocity, const maths::Vec4& kept);
// THAW 0x005E3821: the steep-rail count for the up's y - true (and `*frames` back to 0) when the stall turns
// into a grind.
bool stall_steep_step(float up_y, int* frames);
// THAW 0x005E3585: the stall's facing along the rail - `direction` the way `facing` points (+, or - when
// against it).
maths::Vec4 stall_facing(const maths::Vec4& facing, const maths::Vec4& direction);
} // namespace bike

} // namespace skater::thaw_bmx
