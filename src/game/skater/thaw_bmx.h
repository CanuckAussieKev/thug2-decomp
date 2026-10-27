#pragma once

#include <cstdint>

#include "game/mth/vector.h"

namespace Mdl {
struct FrontEnd;
}
namespace Obj {
class CCompositeObject;
class CSkater;
} // namespace Obj
namespace Script {
class CArray;
class CScript;
class CStruct;
} // namespace Script

// NOT ORIGINAL - THAW's BMX ridden in THUG2 (ours, added at the user's request; not the game's and not PartyMod's):
// phase 2, a rideable bike with the skater's handling.
//
// THAW's bike is a mode of the skater's core physics (a byte at its component's +0x524, set by the member SkaterOnBike)
// with a physics parameter set of its own (bicycle_physics) and the bike a special item. Here:
// - the bike state lives in a side structure keyed by the core physics component (our component's layout is THUG2's;
//   THAW's offsets don't apply), BikeState below;
// - the 17 bike member functions of THAW's core physics (SkaterOnBike ... exitbikeendo) are answered by
//   CorePhysicsComponent::CallMemberFunction through CallMemberFunction below - registered as script member
//   functions only while the feature is on; plus ThawBmxLoadAssets (ours: the bike's skeleton and animations loaded
//   once, permanent);
// - GetPhysicsStructure (0x005802E0, skater.cpp) gives thaw_bicycle_physics (THAW's bicycle_physics, put into the
//   overlay's scripts by tools/pipeline/thaw_bmx/convert.ts) while the skater is on the bike;
// - the ride itself is THAW's own bike scripts (phase 4: bike_scripts.qb / bike_grindscripts.qb / bike_sfx.qb ported
//   by tools/pipeline/thaw_bmx/bike_scripts.ts - bike_start, bike_onground, BIKE_OLLIE, bike_air_state, BIKE_GRIND, bike_manual,
//   the trick lists...), their animation-tree calls (Skater_PlayBikeAnim and THAW's Skater_Playanim family) played with
//   THUG2's PlayAnim on the rider and on the bike (thaw_bmx_details, phase 1); the bike's partials (wheels, bars) as
//   anim subsets - all scripts, in tools/pipeline/thaw_bmx/convert.ts's thaw_bmx.ns. The manual's pose (THAW 0x00620940: the
//   wobble animation's place from the manual angle) is EndFrame's (thaw_bmx_tricks.cpp).
// "On the bike" is the bike flag alone (THAW's +0x524; phase 2 and 3 rode as THUG2's vehicle "thaw_bmx" too, phase 4
// dropped that): the skater's reset (CSkater::ResetComponents - a retry, a level change: its special items, the bike,
// destroyed) ends the ride.
//
// Getting on / off: the key [Keybinds] ThawBmx= (an SDL scan code or key name; default F7) in a level, the pause
// menu's RIDE BMX / GET OFF BMX (make_menu_level), or the script thaw_bmx_toggle (the test-automation channel's
// `run thaw_bmx_toggle`). Single player only (the bike state isn't sent to other players).
//
// thug2-decomp.ini [features] thaw_bmx=1 turns it on (default 0: nothing registered, the physics structure, the
// member functions and resets are THUG2's own).
namespace Skater::ThawBmx {

// THAW's wheel points sit this far ahead of / behind the skater's position along its facing (THAW.exe 0x0069014C).
constexpr float kWheelOffset = 24.0f;
// THAW's ResetBikeManualAngle value (0x006906C8).
constexpr float kManualAngleReset = 50.0f;
// THAW's BikeExitSkid sets the skid's timer (+0x554) to this.
constexpr int kSkidTimerReset = 60;

// A digital direction of the movement stick as THAW's pad has them (its LEFTUP / LEFTDOWN / LEFTLEFT / LEFTRIGHT
// buttons, pad +0x280..+0x2E0 - THUG2's pad has no such buttons: thaw_bmx_physics.cpp makes them from the d-pad and
// the stick): held, and the game clock (with its offset) when it went down.
struct StickButton {
    bool down = false;
    int pressTime = 0;
};

// THAW's bike state on its core physics component (offsets of THAW's component in the comments).
struct BikeState {
    bool on = false;            // +0x524 SkaterOnBike / SkaterOffBike
    bool manual = false;        // +0x525 the bike's manual (the manual pitch, ManualPitch, sets it)
    bool noseManual = false;    // +0x526
    bool xup = false;           // +0x527 BikeEnterXup / BikeExitXup
    bool frontBrake = false;    // +0x528 the brake button is the front one (L1; R1 the rear)
    bool tireTapNose = false;   // +0x529 (the tire tap began with up held: on the front wheel)
    bool tireTap = false;       // (THAW: its timed flag +0x90)
    bool tapArmed = false;      // +0x54C in vert: the tire tap looked for every frame (once asked), buttons or not
    bool tapLedge = false;      // +0x54D the tap point is a ledge's (a rail's), not a surface below
    float tapPitch = 0.0f;      // (ours) how far the bike has turned over for the tire tap so far (radians)
    bool landedFromTireTap = false; // +0x70D (THAW: +0x54E copied at the landing)
    Mth::Vector tapPoint{0.0f, 0.0f, 0.0f, 1.0f}; // +0x52C where the wheel taps
    Mth::Vector tapStart{0.0f, 0.0f, 0.0f, 1.0f}; // +0x53C where the bike was then
    bool skid = false;          // +0x550 BikeEnterSkid / BikeExitSkid
    bool canSkid = true;        // +0x558 (THAW's constructor sets it)
    int skidTimer = 0;          // +0x554
    bool endo = false;          // +0x4D0 / +0x4E4 (exitbikeendo clears them)
    Mth::Vector endoVelocity{0.0f, 0.0f, 0.0f, 1.0f}; // +0x4D4 the velocity the endo began with
    float manualAngle = kManualAngleReset; // +0x56C
    int noBalanceFrames = 0;    // +0x18CC frames the manual pitch waits after a manual starts
    bool overGrind = false;     // +0x18C8
    float airPitch = 0.0f;      // +0x66C the pitch in the air so far (degrees; 0 on the ground)
    Mth::Vector frontWheel{0.0f, 0.0f, 0.0f, 1.0f}; // +0x18D0
    Mth::Vector rearWheel{0.0f, 0.0f, 0.0f, 1.0f};  // +0x18E0
    Mth::Vector lastFrontWheel{0.0f, 0.0f, 0.0f, 1.0f}; // +0x18F0 (at the frame's start)
    Mth::Vector lastRearWheel{0.0f, 0.0f, 0.0f, 1.0f};  // +0x1900
    StickButton stickUp, stickDown, stickLeft, stickRight; // (THAW's pad +0x280 / +0x2A0 / +0x2C0 / +0x2E0)
    bool vertGroundNoted = false; // (ours: THAW's constructor 0x005CF810 sets +0x6F8 - never on vert ground yet)
    int vertGroundTime = 0;       // +0x6F8 the game clock (with its offset) last on vert ground
    float steerBlend = 0.5f;      // +0x570 the turn animation's share of the turn branch (THAW 0x005D6360), 0.5 .. 1
    bool steerAnim = false;       // (ours) bike_playturnanim's turn animation on (the member ThawBmxSteerAnim)
    float steerIn = 0.0f;         // (ours) how far THAW's tree has blended the turn branch in (0 .. 1)
};

// [features] thaw_bmx (default 0; the self-tests turn it on for a moment).
bool& Enabled();
// The bike state of `core` (a core physics component); null when it has none and `create` is false.
BikeState* StateOf(const void* core, bool create);
// Its state forgotten (a component made at the same address starts off the bike).
void Forget(const void* core);
// Whether `core`'s skater rides the bike: the bike flag (SkaterOnBike).
bool OnBike(const void* core);
// The physics' gate (thaw_bmx_physics.cpp): the feature on and the skater on the bike. Off: the skater's physics,
// unchanged.
inline bool Active(const void* core) { return Enabled() && OnBike(core); }
// The physics structure while on the bike (thaw_bicycle_physics; null off the bike or when the overlay hasn't it).
Script::CStruct* PhysicsStructure(const void* core);
// A bike member function on `core` (1 / 0 its result), or 2: not one of ours.
int CallMemberFunction(void* core, uint32_t name, Script::CStruct* params, Script::CScript* script);
// The wheel points from the skater's position and facing (THAW 0x005C9A50's second half).
void SetWheelPoints(BikeState& state, const Mth::Vector& position, const Mth::Vector& facing);

// At start-up, after the game's own: ThawBmx_Available (a C function, always: false while the feature is off),
// ThawBmx_LoadTexture (always: LoadTexture unless that texture is loaded - the bike manual meter's pictures) and,
// while it's on, the bike's member functions.
void RegisterScriptFunctions();
// From CSkater::ResetComponents (its special items destroyed - the bike with them): off the bike, out of the vehicle,
// the board and the camera back (the script thaw_bmx_after_reset).
void OnSkaterReset(Obj::CSkater* skater);
// Once a frame from the front end's input logic (MouseMenus::Update): the key.
void Update(Mdl::FrontEnd* frontEnd);

// ---------------------------------------------------------------------------------------------------------------
// Phase 3 (thaw_bmx_physics.cpp): THAW's bike branches in the skater's core physics, re-expressed on THUG2's code.
// Each is called from our THUG2 function at the place THAW's branch sits, only while Active(core) - otherwise the
// THUG2 function runs exactly as before. THAW's addresses are in each function's comment.

// At the start / end of the core physics' update (0x00510830; THAW 0x005E87B0): the movement stick's directions as
// THAW's pad buttons, the wheel points kept (THAW +0x18F0 / +0x1900) / the x-up let go (THAW 0x005C9380).
void BeginFrame(void* core);
void EndFrame(void* core);

// On the ground (DoGroundPhysics 0x0050EE70; THAW 0x005E4370..0x005E4F2F):
// THAW 0x005E4370: the air pitch back to 0, the tire tap over.
void OnGround(void* core);
// THAW 0x005E4900: whether the ground's gravity pulls the bike along this frame - not when it's slower than 10 and
// the gravity points back along it, unless in a manual without a brake held (the bike doesn't roll back).
bool GroundGravityApplies(void* core, const Mth::Vector& gravity);
// THAW 0x005CACD0 (the brake check, on the bike): L1 the front brake, R1 the rear, when the skater may brake (+0x311).
bool ShouldBrake(void* core);
// THAW 0x005CAE10 (Brake, on the bike): not the rear brake in a nose manual nor the front one in a manual; the skid
// brake out of a manual, the front brake in an endo.
void Brake(void* core);
// THAW 0x005CB060 (whether to pedal, on the bike): not in a manual, rolling backwards or with a brake held.
bool CanKick(void* core);
// THAW 0x005CB1C0 (the pedalling's push, on the bike): not rolling backwards or in a manual.
bool MayAccelerate(void* core);
// THAW 0x005D6360 (the bike's own turning on the ground, THAW 0x005DA890 calls it on the bike): by the stick, sharper
// slower and with a brake held; rolling backwards the other way round (steered by the manual's balance outside one);
// the skid's timer; the wheel points turned along.
void TurnOnGround(void* core);
// THAW 0x005CDBC0: the manual pitch (stick back: a manual, forward: a nose manual, the front brake fast: an endo).
void ManualPitch(void* core);

// Jumping (DoJump 0x00509450; THAW 0x005DDF30): from a standstill a boost along the facing; out of a manual the
// skater pitched by the manual's angle.
void JumpBoost(void* core);
void JumpPitch(void* core);

// In the air (DoAirPhysics 0x0050CA00; THAW 0x005DF9F0 calls 0x005D7E20 on the bike instead of its lean and spin):
// no manual in the air; the bike's pitch and spin by the stick (the fast ones with L1), back upright when let go, a
// bail when the rider's head hits something.
void OnAir(void* core);
void AirRotation(void* core);

// The ground under the bike (CheckForGround 0x00509BD0; THAW 0x005DF1C0 on the bike, out of a manual): the two wheels
// felt for the ground (THAW 0x005D97C0) - kWheelsOff: the leading one gone, off the ground; kWheelsOnGround /
// kWheelsOnGroundNoHit: the skater midway between them, the ground normal across the wheel base (the feeler under the
// skater hit or not); kWheelsNotUsed: THUG2's own check (in a manual, or [features] thaw_bmx_wheels=0).
// [debug] thaw_bmx_wheels=1 logs each frame the wheels leave the ground (`thaw_bmx wheels off:` - each wheel's hit,
// why not, distance, reach, turn).
enum WheelGround { kWheelsNotUsed, kWheelsOff, kWheelsOnGround, kWheelsOnGroundNoHit };
WheelGround WheelGroundCheck(void* core);
// Whether the skater rolls backwards on the bike (the velocity against the facing) - THAW's ground check (0x005DEC78)
// turns the facing round for its steepness test then.
bool RollingBackwards(void* core);
// THAW 0x005CB590 (the display normal's lerp, on the bike on the ground): towards the ground normal across the
// wheel base, at Normal_Lerp_Speed_Bike. Returns false when THUG2's own (0x004FEA50) is to run.
bool LerpDisplayNormal(void* core);

// THAW 0x005CF460 (the grind trick, on the bike): the over-grind state set, and BikeGrindTrickList's row for how far
// the stick is pushed up or down with `*column` its element - null when the overlay hasn't the list (THUG2's
// GrindTrickList then). `leftSide`, `sideways`, `backwards` as StartGrindTrick's first three.
Script::CArray* GrindTrickRow(void* core, bool leftSide, bool sideways, bool backwards, uint32_t* column);
// THAW 0x005E2965: a bad ledge on the bike's side: the grind on the other side.
bool GrindSide(void* core, bool leftSide, bool badLedgeOnThatSide);
// THAW 0x005D1A00: the acid drop's search direction on the bike - its facing, flattened; the other way with the stick
// pulled back. False: not on the bike.
bool AcidDropDirection(void* core, Mth::Vector* direction);

// THAW 0x005CA520, the member EnterWheelieFromAir (the bike's manual from a landing; `nose` its parameter).
int EnterWheelieFromAir(void* core, Script::CStruct* params);

// ---------------------------------------------------------------------------------------------------------------
// Phase 4 (thaw_bmx_tricks.cpp): what THUG2's vehicle mode used to hide - each THAW's own bike gate -, the tire tap,
// the manual's pose, the bike's sounds and the THAW-only members its bike scripts ask. Called only while Active(core)
// (the sounds: while the skater is on the bike); otherwise THUG2's code runs unchanged.

// THAW 0x005D713F (the wallride's start): on the bike square held starts a wallride too (THUG2's: the triangle).
bool WallrideButton(void* core);
// THAW 0x005DB35D (the wallplant): on the bike square held plants without asking the trick list.
bool WallplantBySquare(void* core);
// ... else the bike's list, bike_Wallplant_Trick (THAW's; THUG2's Wallplant_Trick when the overlay hasn't it).
uint32_t WallplantTrickList();
// THAW 0x005D34A0: on the bike skitching is R2 held long enough (the skater's: up).
uint8_t* SkitchButton(void* core);
// THAW 0x005DFAFB (in the air, after the recovery): the tire tap - looked for (0x005DC400: R1 and up / down held,
// a ledge ahead - its search is behind an obfuscated site, inferred) or carried on (0x005CEA90: the wheel to the tap point).
void TireTap(void* core);
// Whether a tire tap is on (THAW's timed flag +0x90; 0x005DFB4B: the air's keep-upright skipped then).
bool TireTapping(const void* core);
// THAW 0x00620940: the manual's pose - the wobble animation (bike_manual's bmx_wheelie_init / nosewheelie ones played
// with `wobble`) held at the manual angle x 0.01 of its length, on the rider and the bike (EndFrame calls it).
void ManualPose(void* core);

// The bike's sounds (THAW's skater sound component 0x005F1A9B / 0x005F1B30 / 0x005F1BD0 and its looping sound
// 0x0046A1B7 / 0x0046A1F3: on the bike BIKE_LAND, BIKE_GRINDLAND, BIKE_OLLIE, BIKE_GRINDJUMP, BIKE_BONK, BIKE_ROLL,
// BIKE_GRIND - TERRAIN_BIKE_DEFAULT_*, every terrain the same; nothing in the air), from THAW's tables copied into the
// overlay (thaw_bmx_terrain_land ...).
enum class BikeSound { Land, GrindLand, Ollie, GrindJump, Bonk, Roll, Grind, Air };
// For the skater object `skater`: -1 not on the bike (THUG2's terrain sound), 0 no sound, 1 the sound in `info`
// (a Terrain::SoundInfo).
int BikeSoundOf(const void* skater, BikeSound kind, void* info);

// The bike drawn into the rider's shadow (Nx::SetShadowCompanion, registered while the feature is on): for the
// local skater's model on the bike, the bike's model while it's shown; else null.
void* ShadowCompanion(void* casterModel);

// The THAW-only members of the bike scripts besides the bike's own (CallMemberFunction answers them; 2: not one):
// DisplayPitchGreaterThan (0x005CA600), LandedFromTireTap (0x005C7C60), InTireTap (its state component's +0x90),
// PitchingForward / PitchingBackward (its matrix queries), ShouldRunStallScript / ClearStallScriptFlag /
// ExitStallBoost (0x005C8580 / 0x005C8590 / 0x005C8540: the bike's stalls aren't ported - never one), OnStall.
int CallTrickMember(void* core, uint32_t name, Script::CStruct* params, Script::CScript* script);

// ---------------------------------------------------------------------------------------------------------------
// Phase 6 (thaw_bmx_physics.cpp): steps, crests and vert.

// THAW 0x005C2980 (the adjust-physics component's ground-below check, the "uber frig", on the bike): its feeler starts
// Skater_First_Forward_Collision_Height higher - the bike's position, midway between its wheels, can sit below the
// ground's surface on a step or over a crest (the wheels on the ground, the line between them through it). Ours also
// raises the "under the floor" check's end by as much (THAW doesn't - phase 6: the
// same midway position under a step's top would turn the bike back there). 0: not on the bike.
float StuckCheckRaise(void* core);
// THAW 0x005CAA60 (LimitSpeed, THAW's for every skater - here on the bike only, the keys being bicycle_physics'):
// for Skater_Vert_Max_Speed_Time after the bike was last on vert ground (THAW's ground update 0x005E46E9 notes the
// time while its feeler hits vert) the max-max speed is Skater_Vert_Max_Max_Speed_Stat and the heavy air friction
// over the max speed isn't applied. `*maxMax` changed; returns whether the heavy friction may apply this frame.
bool VertSpeedAllowance(void* core, float* maxMax);
// THAW 0x005E46E9: on the ground, the time noted while the ground feeler's face is vert.
void NoteVertGround(void* core);

// ---------------------------------------------------------------------------------------------------------------
// Phase 7 (thaw_bmx_physics.cpp, thaw_bmx_tricks.cpp): THAW.exe's code read in a second THAW PC build.

// THAW 0x005E05E6: in the air, once the display matrix is the skater's again, the wheel points follow the skater.
void AirWheelPoints(void* core);
// THAW 0x005DA380 (break vert, on the bike): the movement stick's up held longer than `pushTime` ms (neither left nor
// right, square nor circle) - THUG2's d-pad up hold.
bool VertPushHeld(void* core, int pushTime);
// ... and R1 with the stick up or down, or a tire tap looked for in vert: the spine transfer / push-off path.
bool VertTapHeld(void* core);
// (The manual meter is THAW's own, drawn by its bike scripts - create_bike_manual_meter / update_bike_manual_meter in
// bike_manual_state: its pictures turned by 0.9 degrees per point of the manual angle, GetBikeManualAngle. The DLL
// only loads its pictures: the C function ThawBmx_LoadTexture, thaw_bmx.cpp.)

// ---------------------------------------------------------------------------------------------------------------
// Riding backwards and the turn's lean (thaw_bmx_physics.cpp). Riding backwards: THAW's 0x005CCF40 (THUG2's
// 0x004FF920, the turn round to fakie) does nothing on the bike - DoGroundPhysics skips it; the bike stays in THAW's
// backwards balance (bike_backwards_balance_trick) until it stops, loses the balance or R2 pivots it round.

// The bike's own object (the special item ThawBmx on `skater`), or null.
Obj::CCompositeObject* BikeObjectOf(void* skater);
// THAW's turn branch (bike_playturnanim's Skater_PlayTurnAnim: a TurnBlend of <anim> and <turn_anim>, its share the
// physics' +0x570 - THAW 0x0061FAC0 / 0x00620080, 2nd build) on THUG2's animation: bike_playturnanim plays the turn
// animation in the anim subset ThawBmxTurn on the rider and the bike (thaw_bmx_playturnanim) and asks for it with the
// member ThawBmxSteerAnim (on / off: off from its exit script); here, at the end of each frame, the subset's weight is
// set to THAW's share x how far its tree would have blended the branch in (0.4 s) or out (kSteerOut) - and the subset
// removed once it's out. EndFrame calls it.
void SteerAnim(void* core);
// The member ThawBmxSteerAnim (ours): `on` / `off`.
void SetSteerAnim(void* core, bool on);
// The turn animation's subset gone at once (SkaterOffBike, OnSkaterReset - before the state goes).
void SteerAnimOff(void* core);

// The maths of the branches, on plain values (thaw_bmx_physics.cpp; the self-tests check them against THAW's code as
// read).
namespace Bike {
// Phase 7. THAW 0x005D5060 (2nd build 0x005D4EF0): one wheel's ground - the feeler's hit (`point`, `normal`,
// `skatable`) under `wheel` (moved since `last`, the frame's start), the skater's `up`, `facing` (`backwards`: rolling
// backwards), the ground normal, Ground_stick_angle(_forward) and Physics_Ground_Snap_Down.
struct WheelFeel {
    Mth::Vector wheel, last, up, point, normal, groundNormal, facing;
    bool backwards, skatable;
    float stickAngle, snapDown;
};
struct WheelFeelResult {
    bool ground;      // the wheel is on the ground
    bool moveWheel;   // `wheel` is its new place (the hit point plus its normal)
    Mth::Vector wheel;
    // (for [debug] thaw_bmx_wheels=1's log: why not - 1 not skatable, 2 turns away too steeply, 3 out of reach)
    int why;
    float distance, reach, turn, limit;
};
WheelFeelResult FeelWheelOn(const WheelFeel& in);
// THAW 0x005D6360 (2nd build 0x005D61F0): the turn animation's share (+0x570) for the frame's turn rate (radians a
// second before the frame length, GroundTurnRate's) - (|rate / Physics_Ground_Sharp_Rotation| + 1) / 2: half the turn
// pose at the gentlest turn, all of it at the sharpest.
float SteerBlend(float rate, float sharpRotation);
// (ours) THAW's anim tree blending the turn branch in over its BlendPeriod 0.4 while `on`, out over kSteerOut once
// not: `in` (0 .. 1) a frame of `frameLength` seconds on.
float SteerIn(float in, bool on, float frameLength);
// THAW 0x005CAE10: which brake acceleration (0 none: the brake is skipped, 1 Physics_Brake_Acceleration, 2
// Physics_Front_Brake_Acceleration, 3 Physics_Brake_Skid_Acceleration).
int BrakeChoice(bool manual, bool nose, bool frontBrake, bool endo, bool balanceIsManual);
// THAW 0x005D6360: the turn rate (radians a second; + left) before the frame length.
float GroundTurnRate(float speed, float maxSpeed, float rotation, float sharpRotation, bool braking, bool manual,
                     float stickX, int heldMs, bool backwards, float balanceLean);
// The manual pitch's input and what it did (THAW 0x005CDBC0).
struct ManualInput {
    bool manual, nose, endo;   // the state before
    float angle;               // the manual angle (+0x56C)
    int noBalanceFrames;       // +0x18CC
    bool up, down, left, right; // the stick's directions (THAW's LEFTUP ...)
    int upHeld, downHeld;      // how long up / down has been held (ms)
    bool frontBrake;           // L1 held
    float stickY;              // the stick's analog y (THAW's pad +0x35C, -1 up .. 1 down)
    float speed;               // the velocity's length
    bool forward;              // the velocity along the facing (> 0)
    float noLeanTime;          // Physics_Ground_No_Lean_Time
    int noBalanceStart;        // Physics_manual_no_balance_frames
    float lean;                // Physics_Ground_Lean_stat
    float frameLength;
};
enum class ManualEvent { None, EnterManual, EnterNoseManual, EnterEndo, Bail, Landed, ExitEndo };
struct ManualOutput {
    bool manual, nose, endo;
    float angle;
    int noBalanceFrames;
    ManualEvent event;
};
ManualOutput ManualStep(const ManualInput& in);
// THAW 0x005DDF30: whether a jump gets the standstill boost (no horizontal speed at all, the stick not pulled back).
bool JumpBoosts(const Mth::Vector& velocity, float stickY);
// THAW 0x005D7E20: the air pitch's settling back towards upright (degrees to add to the pitch so far) once let go.
float SettleAirPitch(float pitch);
// THAW 0x005CF460: BikeGrindTrickList's row for the stick's y (-128 up .. 127 down): 0 centred, 1 / 2 a little /
// far up, 3 / 4 a little / far down.
int GrindRow(float stickY);
// NOT ORIGINAL (ours): the stick's y GrindRow is given with the d-pad (or keys) - the stick centred: THAW's own reach
// is full up / down only, so Smith and Feeble (a little up / down) could never be had; here up or down with left or
// right (a diagonal) is half way. The analogue stick's y as it is.
float GrindStickY(float rawY, bool up, bool down, bool side);
// ... and the column (bit 0 the side, bit 1 sideways) and whether it's an over-grind.
int GrindColumn(bool leftSide, bool sideways, bool backwards, bool* overGrind);
// Phase 6. THAW 0x005CAA60: `since` ms after the last vert ground, with Skater_Vert_Max_Speed_Time `window` - `*raise`:
// the vert max-max speed; returns whether the heavy air friction over the max speed may apply.
bool VertWindow(float since, float window, bool* raise);
// THAW 0x005CB590: the ground normal with its part along the wheel base taken out (normalised).
Mth::Vector WheelBaseNormal(const Mth::Vector& normal, const Mth::Vector& front, const Mth::Vector& rear);
// THAW 0x005DF550: the up across the wheel base (right x the base's direction, normalised), the way nearer `hit`.
Mth::Vector WheelBaseUp(const Mth::Vector& right, const Mth::Vector& front, const Mth::Vector& rear,
                        const Mth::Vector& hit);
// Phase 4. THAW 0x005CEA90: the tire tap's slowing each frame - the horizontal speed down by 50 (at least 0; THAW
// takes 50 off x and z each and clamps each at 0, which stops a bike going towards -x or -z dead: ours the length,
// as meant), and rising: by a tenth over 100, else by 15.
Mth::Vector TireTapVelocity(const Mth::Vector& velocity);
// THAW 0x005CEA90: where the bike goes towards the tap point this frame (ours: the result's position; `ends` set when
// the tap is over - the point above the bike while still far off; `lands` when it's reached the point above it).
struct TapStep {
    Mth::Vector position;
    bool ends, lands;
};
TapStep TireTapStep(const Mth::Vector& position, const Mth::Vector& tap);
// ShadowCompanion's rule: `bikeModel` when the caster is the rider's model and the bike's is shown (its flags 0x2 and
// not 0x4).
void* ShadowCompanionOf(const void* casterModel, const void* riderModel, void* bikeModel, uint8_t bikeModelFlags);
// THAW 0x00620940: the manual's wobble fraction (the manual angle x 0.01).
float ManualWobble(float manualAngle);
// THAW 0x005CA600: whether the angle between the display's up and the display normal is beyond `degrees`.
bool DisplayPitchBeyond(const Mth::Vector& up, const Mth::Vector& normal, float degrees);
// The tire tap's ledge (THAW 0x0058C9A0's search; its first instruction an obfuscated site, the rest plain): the nearest
// point on a rail segment from `a` to `b` to the line `start`-`end` (within `margin`; the part along `direction`
// counting a third), not above the line and at most 12 below it - as THUG2's own ledge search (the walk component's
// hang check, which THAW passes it) takes rails in the air. False when it's out of reach. `score`: how far (squared).
bool LedgePoint(const Mth::Vector& start, const Mth::Vector& end, const Mth::Vector& direction, const Mth::Vector& a,
                const Mth::Vector& b, float margin, Mth::Vector* point, float* score);
} // namespace Bike

} // namespace Skater::ThawBmx
