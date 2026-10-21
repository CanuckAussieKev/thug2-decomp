# Object components, the start of the range (0x0041A6D0-0x00423643) - objects-1 agent (OC1)

Branch worktree-agent-ac9022c3107f3faca. The range is the first part of "Object components & level/scene loading":
small composite-object components, each with its vtable, constructor, destructors, update and script member functions
(the members of the table-registered components are written in by static initialisers in 0x00643xxx, outside the
range). Tests: src/hook/selftests_obj_*.cpp (original vs ours on one stand-in component and object, every callee a
recording stand-in - ours and the game's address alike -, traces with return values and the component's / object's
memory compared). Shared files touched (small, additive): CMakeLists.txt (new files listed), src/game/mth/matrix.{h,cpp}
(Matrix::InitFromAngles), src/game/mth/vector.{h,cpp} (Mth::DistanceSquared), src/game/obj/objman.{h,cpp} (the object
search's destructors).

The range's classes / topics:
- 0x0041A730 / 0x0041A820: Mth helpers (Matrix(angles), DistanceSquared).
- 0x0041A810-0x0041B274: the bounding box component (0xB87C8E43 "bbox"): Obj_ObjectInRadius / InRadiusOfNode / InRect;
  the object search's destructors (0x0041A850, 0x0041AA90, 0x0041AAA0); Mdl::Skate::GetNumSkaters (0x0041AD90).
- 0x0041B280-0x0041B580: the colour cycle component (0xCDC76A22).
- 0x0041B6D0-0x0041B8A0: the camera component (0xC4E311FA).
- 0x0041B910-0x0041C1B0: the car physics component (0xC695136D): wheel spin, steering, body tilt on the car's bones.
- 0x0041C1F0-0x0041C700: the collide-and-die component (0x6259B52B); 0x0041C350 (82 callers): a smart pointer's reset.
- 0x0041C710-0x0041CB75: collision and matrix components (done before this agent).
- 0x0041CB90-0x0041D1D0: the 0xD8BFC9F7 component.
- 0x0041D1D0-0x0041D6D0: the fam component (0x0301D67C: Obj_PlayFam / Obj_StopLastFam, fam\%08x.fam files).
- 0x0041D6E0-0x0041E240: the flexible particle component (0xC5BCEB9E).
- 0x0041E270-0x0041E6C0: the 0xEADA1621 component.
- 0x0041E6F0-0x0041F390: the input component (0x27D7CD28).
- 0x0041F400-0x0041FF90: the lockobj component's remaining pieces (its update 0x0041F760).
- 0x0041FFA0-0x00422D00: the model component (0x286A8D26).
- 0x00422D00-0x004230B0: the model light update component (0xE3FCA939).
- 0x00423130-0x00423643: the motion component's member-function registration and its first members.

Switch off: `[replacements] disable=bbox_component.cpp,color_cycle.cpp,camera_component.cpp,car_physics.cpp,collide_and_die.cpp,dynamic_light.cpp,fam.cpp,flexible_particle.cpp,floating_text.cpp,input_component.cpp` (any of them alone; the files of later batches are added here).

- [x] Batch OC1-1 (NOT run in the game yet; the bounding box component, 19 functions): src/game/obj/bbox_component.{h,cpp}
  (new), src/game/skate/skate_module.{h,cpp} (new: Mdl::Skate's skater list only), objman.cpp, matrix.cpp, vector.cpp.
  - Obj::CBBoxComponent (type 0xB87C8E43 "bbox", vtable 0x00645DC4, 0x4C bytes; every composite object gets one,
    AddBaseComponents): 0x0041ADC0 Create, 0x0041AAD0 Construct (its box +-10 about the origin and an offset (0, 0, 0,
    1) - set nowhere else in the exe), 0x0041A810 / 0x0041A880 the destructors, 0x0041B1A0 CallMemberFunction:
    Obj_ObjectInRect (0x6520B902), Obj_ObjectInRadius (0x67660FE5), Obj_ObjectInRadiusOfNode (0xDCE760A1; no node=:
    0); 0x0041B0A0 QueryObjects (radius= feet, squared; type= a checksum or an array, asserted), 0x0041AF10
    AnyObjectNear (skater / ped / gameobj / car - the object types 0xD2DB7F97 / 0xBC0D91D8 / 0x5C0312EB through the
    object search over the object's manager; anything else false; the params / script arguments unused), 0x0041AE20
    AnySkaterNear (the skate module's skaters by index), 0x0041A950 InitRect (offset feet, angle degrees, height /
    dist / width feet), 0x0041AB20 ComputeRect (the object's orientation turned by the angle about Y - D3DX's multiply
    -, inverted; the box (width / 2, height / 2, dist) to (-width / 2, -height / 2, 0) about the offset, w 1 / -1 + the
    offset's w), 0x0041A8B0 RectRadiusSqr (cdecl; the largest size + the largest offset component, squared - the first
    call returns it unrounded from the FPU stack, later calls the stored float), 0x0041AC40 IsInRect (first the
    bounding distance from the box's middle - x and the middle's z rounded to float where the original stores them -,
    then the middle of the other object's own box brought into the rect's frame, PointInBox 0x004817D0; the other
    object's bbox component is read without a null check, as in the original).
  - 0x0041A850 / 0x0041AA90 / 0x0041AAA0: Obj::CObjectSearch's destructors (its base Lst::Search<CObject>'s vtable
    0x00645DF8; its own 0x00645DFC). 0x0041AD90 Mdl::Skate::GetNumSkaters (the list head at +0x28; a node's data is
    checked for -1 before the node for null, as the original). 0x0041A730 Matrix::InitFromAngles (the constructor
    Matrix(angles): the identity, then RotateX / Y / Z through D3DX - all skipped when all three are exactly 0; -0 counts
    as 0, NaN not). 0x0041A820 Mth::DistanceSquared ((dx dx + dz dz) + dy dy, unrounded; tracker.cpp has its own copy).
  - Tests (selftests_obj_bbox.cpp): TestBBoxComponent - 48 seeds: Create (and out of memory), the destructors
    (flags 0 / 1 / 2), InitRect from random parameter sets, ComputeRect / RectRadiusSqr from six rect states (computed
    or not, angle 0 / -0 / other, NaN height), IsInRect for six objects (some inside the bounding distance), two at
    exactly the bounding distance, objects at exactly the radius (13 from the point; 169 and just above), the skaters
    and each type with and without a rect / point / radius, QueryObjects and the four members (and another) from random
    parameter sets (radius, type checksum / array / none, node); TestBBoxHelpers - the search destructors, the
    skater count (0-5), Matrix(angles) (zeros, -0, one angle, NaN), DistanceSquared (unrounded results).
  - Mutations: 45 deliberate bugs, 44 caught (40 at first; floats in sevenths - values needing more than a float's
    precision - and exact-distance cases added for 4). 1 equivalent: `>=` for `>` in RectRadiusSqr's largest size (the
    same value either way).

- [x] Batch OC1-2 (NOT run in the game yet; the colour cycle, camera and car physics components, 31 functions):
  src/game/obj/color_cycle.{h,cpp}, camera_component.{h,cpp}, car_physics.{h,cpp} (new).
  - Obj::CColorCycleComponent (type 0xCDC76A22, vtable 0x00645E00, 0x7C bytes): 0x0041B3C0 Create (the constructor
    inlined), 0x0041B280 RegisterMemberFunctions (the table 0x006845D0; its functions written in by the static
    initialiser 0x00643D60), 0x0041B2A0 / 0x0041B2E0 the destructors, 0x0041B310 SetColors (up to five; vectors' x, y,
    z, w 1 - more than five overrun the fields after, as in the original), 0x0041B440 SetOn (off: the object's colour
    back to 0x808080 through the SetObjectColor helper 0x005A3E80 with name= / color=; on: from the first colour, 0
    ms), 0x0041B580 Update (the current colour (z << 8 + y) << 8 + x, each _ftol-truncated; milliseconds with slow
    motion, compared with the period before rounding; then the next colour), the members 0x0041B560 [0x4AF8B9AE] on,
    0x0041B570 [0x1DE2F35B] off, 0x0041B2B0 [0x4A752AAE] the period (the unnamed float, asserted - the original's slot
    for it is the params argument: without one the period takes the pointer's bits), 0x0041B390 [0x80D5F732] the
    colours (the unnamed array, asserted - without one the params pointer is taken for the array).
  - Obj::CCameraComponent (type 0xC4E311FA, vtable 0x00645E34, 0x1C bytes): 0x0041B840 Create, 0x0041B730 Construct (a
    Gfx::Camera of its own; null when out of memory), 0x0041B6D0 / 0x0041B7C0 the destructors (the camera deleted
    through its vtable), 0x0041B7F0 InitFromStructure (the object's priority -1000 with the composite object manager,
    its slot 4), 0x0041B810 Finalize / 0x0041B8A0 Update (the camera at the object's position and orientation; Update
    also makes the orientation the display matrix).
  - Obj::CCarPhysicsComponent (type 0xC695136D, vtable 0x00645E6C, 0x58 bytes): 0x0041BD90 Create, 0x0041B910
    RegisterMemberFunctions (0x006846A8), 0x0041BAD0 Construct (CreateCar's), 0x0041B930 / 0x0041BB00 the destructors,
    0x0041B940 InitFromStructure (the tilt's limits / rate / divisor: script globals 0x877DB8DA, 0xFB3464DF,
    0xFD8BC8D6, 0xB2C7447B, overridden by params), 0x0041BE10 Finalize (the motion, suspend, skeleton and model; the
    distance between bones 2 and 4, each times bone 0 - D3DX), 0x0041C1B0 Update (not when suspended - the suspend
    component's +0x30 or 0x00433EA0 -, dead or without a skeleton), 0x0041BB30 UpdateAngles (the spin from the
    motion's speed, kept in 0-360 by steps on the FPU stack; the steering towards -asin(the bone distance / the
    distance to the path follower's point) - the game's own _CIasin 0x00623560 - negated for a negative turn, 0
    without a follower; the tilt towards -acceleration / divisor within the limits - the globals re-read while
    0x4CF13D8F is set; both through Mth::KeepWithin at 60 degrees a second (times the rate for the tilt)), 0x0041C0B0
    UpdateBones / 0x0041BF00 UpdateSkeleton (cdecl: the bones' matrices from the model's description, four wheel
    bones turned about X by the spin, two steering ones about Z by the steering then X by the spin, the body 0x88C21962
    about X by the tilt, each but the first times its parent; a hidden body's bone 0 scaled to nothing), the members
    0x0041B9E0 [0xA2331896] the body shown (the unnamed integer), 0x0041BA20 [0xC9F0DB4B] nothing.
  - Tests: selftests_obj_color_camera.cpp (TestColorCycleComponent - 40 seeds: Create on dirty memory, the
    destructors, the members with and without their parameters, 10 random on / off / update steps each - periods
    reached exactly, just short of them before rounding, NaN; structures from the pool or the manager (or none);
    TestCameraComponent - 8 seeds), selftests_obj_car_physics.cpp (TestCarPhysicsComponent - 40 seeds: Create /
    Construct on dirty memory, InitFromStructure with and without params, Finalize with and without a skeleton,
    UpdateAngles from 8 states each (spin past 360 by several turns, exactly 720 / 1080, negative, NaN; no follower,
    no turn, NaN turn, the bone distance huge / 0 / negative, the follower's point on its own position; divisor 0 /
    tiny, wide limits, the globals re-read or not), UpdateBones shown / hidden, UpdateSkeleton, Update's early outs; the
    C runtime's asin stood in - the game's isn't started offline).
  - Mutations: 63 deliberate bugs, 57 caught (51 at first; dirty Calloc memory, a frame landing just short of the
    period, exactly 720 / 1080, wide tilt limits added for 6). 6 equivalent: Script::PrintContents removed (empty), the
    camera's null check folded (null either way), the order of the steering distance's squares and `clamped > 1` for
    `ratio > 1` (the ratio is rounded to float before use: the same value), `turn <= 0` for `turn < 0` (0 never gets
    there), the tilt's max compared after rounding (the same value assigned).

- [x] Batch OC1-3 (NOT run in the game yet; the collide-and-die and dynamic light components, the light's radius, the
  feeler's destructor, a smart pointer's assignment, the game clock's time - 18 functions): src/game/obj/collide_and_die.{h,cpp},
  dynamic_light.{h,cpp}, src/game/nx/dynamic_light.h (new); feeler.cpp, smtptr.h / .cpp, game_clock.cpp (additions).
  - Obj::CCollideAndDieComponent (type 0x6259B52B "collideanddie", vtable 0x00645EB4, 0x54 bytes): 0x0041C370 Create,
    0x0041C210 Construct (made at the game clock's time, the first update to come), 0x0041C1F0 / 0x0041C280 the
    destructors, 0x0041C2B0 InitFromStructure (radius, scale, death_script, vel - asserted; vel (0, 0, 1) by default,
    normalised), 0x0041C330 Finalize (the suspend component), 0x0041C3D0 Update: suspended (outside a network game):
    nothing; ten seconds after it was made (unsigned) it dies; dying, a second after the hit; otherwise a feeler from
    the last position (the first time: the object's own) 24 up to the position 24 up plus radius along the
    normalised velocity (x87 roundings where the original stores) - on a hit (movables included) the projectile
    collision component's 0x004F2F40, dying, death_script run with pos (the hit point), vel and scale. The position kept
    for the next update.
  - Obj::CDynamicLightComponent (type 0xD8BFC9F7, vtable 0x00645F58, 0x48 bytes) and Nx::CDynamicLight (layout only;
    made by 0x0048FB80): 0x0041D0F0 Create, 0x0041CBD0 / 0x0041D0C0 the destructors (the light's owner cleared),
    0x0041CBF0 InitFromStructure (StartRadius / StartIntensity / StartRGBA, asserted - the array read whether found or
    not - also the middle and end values unless MidRadius / MidIntensity / MidRGBA / midTime (-1) and EndRadius /
    EndIntensity / EndRGBA are given; the arrays share the original's one slot), 0x0041CDA0 Finalize (the wait-and-die
    component), 0x0041CDC0 Update (the light made the first time, owned by the object; the fraction of the life -
    elapsed / total as unsigned values, FILD's 2^32 correction - from start to end or through the middle (midTime
    compared as -1's bits); radius, intensity and each colour byte (_ftol) interpolated in double; the light at the
    object's position), 0x0041CFC0 CallMemberFunction (0xFAAD1C11 radius=, 0x86E1FA56 intensity=, 0x1A0F4C50 rgba= - the
    light unchecked), 0x0041CB90 CDynamicLight::SetRadius (and 1 / radius; 1e10 for 0).
  - 0x0041C200 Tmr::GameClockTime (elapsed + the offset), 0x0041C350 Nx::CFeeler::Destruct (the object hit let go: 82
    callers, unwind code among them), 0x0041D1D0 CSmtPtr<CRefCounted>::AssignObject (Set returning this; 17 callers).
  - Tests (selftests_obj_collide_light.cpp): TestCollideAndDieComponent - 48 seeds: Create on dirty memory, the
    destructors, InitFromStructure with and without params, 8 random updates each (suspended / in a network game,
    elapsed times at and around 500 / 10000, a hit or not, an object hit, a projectile component, a death script; a
    position 24 below a non-float), the feeler's destructor and the assignment (an object or none, from one or none),
    the game clock's time; TestDynamicLightComponent - 40 seeds: Create, the destructors with and without a light,
    InitFromStructure with each optional parameter missing in turn, 8 updates each (no middle, middles 0 / 0.25 / 0.5 /
    0.7 / 1 / NaN, exactly at the middle, elapsed past 2^31, a total past 2^31), the three members and another,
    SetRadius (0, -0, 3, NaN).
  - Mutations: 49 deliberate bugs, 48 caught (39 at first; 6 that first failed to build - a vc140.pdb lock - caught on
    a rerun; a non-float position and the destructor's results added for 3). 1 equivalent: dropping `&& !m_dying`
    before the hit's handling (a dying component never gets there).

- [x] Batch OC1-4 (NOT run in the game yet; the fam and flexible particle components, 29 functions):
  src/game/obj/fam.{h,cpp}, flexible_particle.{h,cpp} (new).
  - Obj::CFamComponent (type 0x0301D67C "fam", vtable 0x00645F8C, 0x20 bytes): 0x0041D200 Create (constructor inlined),
    0x0041D170 RegisterMemberFunctions (0x006836E8; filled by 0x00642B30), 0x0041D190 / 0x0041D1A0 the destructors,
    0x0041D2C0 UnloadFam (fam_play_stream's scripts on the object stopped, RemoveAnimController type=partialAnim
    id=jawRotation on the animation blender - the object checked -, the asset unloaded), 0x0041D3E0 LoadFam
    (fam\<name>.fam: Ass::CAssMan::LoadAnim(file, name, anim set), its +0x18 kept across; loaded: the file name's
    checksum), 0x0041D480 PlayFam (false without the stream; the last stream stopped; fam_play_stream spawned with
    stream_checksum, vol when not 0, and unless stream-only use_pos_info 0, the fam loaded, the stream preloaded -
    streamId, kept - and play_anim when the fam's animation (anim set + name) is there; the script's object set),
    the members 0x0041D660 [Obj_PlayFam] (unnamed checksum, vol, priority 0x66) and 0x0041D280 [Obj_StopLastFam].
  - Obj::CFlexibleParticleComponent (type 0xC5BCEB9E "flexibleparticle", vtable 0x00645FE4, 0x3C bytes) and
    Nx::CParticleSystem (layout of the fields used; 0x0041D9C0 SetFlag): 0x0041E1E0 Create, 0x0041D6E0
    RegisterMemberFunctions (0x00683AC0; filled by 0x00642FD0), 0x0041DA00 Construct, 0x0041DA80 / 0x0041E240 the
    destructors (the system deleted through its vtable, then let go), 0x0041DB00 InitFromStructure (the system - with
    perm the particle manager's system of that name, else a new one 0x0049AC60 named by the component's address -
    and its settings: groupID, emitterIndependent, emit_rate, params with emitscript / UpdateScript, EnableRotate,
    update_Brightness / ObjID (default: the 0x184197D5 checksum), UseRandomUVQuadrants, FillPath, priority, align_to;
    the manager's update time from Force_Particle_Update_Time), 0x0041D740 Finalize (the suspend component started),
    0x0041DF80 Update (no system: the object dies; time added; suspended outside network games only catching up once
    the time reaches the update time - NaN never does -; else following the object - EnableRotate: the system's
    rotation times the object's orientation (translation cleared) becomes the display matrix it follows - and given
    the time, or idle when the suspend component says not to update; a hidden object's system's +0x12F / +0x130
    cleared), 0x0041D760 Hide, 0x0041D780 CallMemberFunction (IsEmitting - the system unchecked -,
    ApplyEnvBrightness), 0x0041D700 the member-function-entry runner (vtable slot 11, shared with the animation
    blender: the entry's member pointer with its this-adjustment and the entry's name, or slot 12), and the members
    0x0041D7F0 (17 names passed on to the system), 0x0041D820 EmptySystem, 0x0041D840 (0x7F97BAD3 / SetEmitScript),
    0x0041D8F0 ShouldEmit, 0x0041D920 / 0x0041D940 EmitOn / EmitOff, 0x0041D960 destroy (ifEmpty), 0x0041E190
    DestroyParticles.
  - Tests (selftests_obj_fam_particle.cpp): TestFamComponent - 40 seeds: Create, register, destructors, 6 random
    UnloadFam / LoadFam / PlayFam / Obj_PlayFam / Obj_StopLastFam calls each (streams there or not, assets there or
    not, load failing, no object, script structures from the pool or the manager); TestFlexibleParticleComponent -
    48 seeds: Create / Construct (dirty memory), the destructors, InitFromStructure from random parameter sets (perm
    finding a system or not, the system made or not, update_Brightness without ObjID), 8 updates each (no system,
    suspended in / out of network games, the update time reached exactly / NaN, idle, EnableRotate, a brightness
    model, hidden), Hide, every member with and without a system, the entry runner (with an adjustment, through slot
    12), DestroyParticles, SetFlag.
  - Mutations: 80 deliberate bugs, all caught (76 at first; the update time reached exactly, an entry adjustment and
    update_Brightness without ObjID added for 3; one mutation rewritten that didn't compile).
- [x] Batch OC1-5 (NOT run in the game yet; the floating text and input components, 37 functions):
  src/game/obj/floating_text.{h,cpp}, input_component.{h,cpp} (new).
  - Obj::CFloatingTextComponent (type 0xEADA1621, vtable 0x00646018, 0x6C bytes): 0x0041E3C0 Create, 0x0041E5E0 /
    0x0041E6C0 the destructors (the 3D element destroyed by script 0x2575B406 with id=), 0x0041E2B0 InitFromStructure
    (id - the object's id minus 0x1525E9DF by default -, string, color, y_offset, duration), 0x0041E3A0 Finalize,
    0x0041E450 Update (the element made by script 0x6A060CF0 the first time, kept at the object's position raised by
    y_offset with the text "\c<color><string>" while the model is visible; the end time), 0x0041E270
    CallMemberFunction; Front::GetScreenElementManager 0x0041E2A0. The text buffer: the original's sprintf into 64
    bytes overflowed for strings over ~60 characters - ours is cut at 64 (snprintf).
  - Obj::CInputComponent (type 0x27D7CD28 "input", vtable 0x0064605C, 0x4A0 bytes): 0x0041EEC0 Create, 0x0041E6F0
    RegisterMemberFunctions, 0x0041E850 Construct, 0x0041E710 / 0x0041E8C0 the destructors (both input handlers out of
    their lists), 0x0041F390 InitFromStructure (Player / controller), 0x0041F1B0 BindToController (the two handlers,
    vtable 0x0064D7D8, code 0x0041F170 / 0x0041F190), 0x0041F090 / 0x0041F100 the pad updates (paused objects skipped;
    disabled, net-disabled or blocked: the pad reset, blocked also clears the events), 0x0041EF20 IsInputBlocked (in
    network games: the controller-unplugged dialog, the root window's menu_state on, the keyboard anchor), 0x0041ECA0
    UpdateEvents and 0x0041EBF0 CheckButton (16 buttons: locked ones skipped; down / up events to the object when input
    events are on and it isn't disabled), 0x0041E7E0 LockButton (locked until now + seconds - _ftol-truncated - and
    released when asked), and the members 0x0041E8F0 LockButtonFromScript, 0x0041E9A0 / 0x0041E9B0 / 0x0041E9C0 /
    0x0041E9D0 Disable / Enable / NetDisable / NetEnablePlayerInput, 0x0041E7C0 / 0x0041E7D0 Enable / DisableInputEvents,
    0x0041E9E0 PlayerInputIsDisabled, 0x0041EA20-0x0041EA50 Left / Right / Up / DownPressed, 0x0041EA60 GetSticks
    (angles in degrees, lengths), 0x0041EB20 HeldLongerThan (button, time in seconds or frames).
  - Tests (selftests_obj_text_input.cpp): TestFloatingTextComponent - 40 seeds: Create, InitFromStructure from random
    parameters (long strings), Finalize, updates (model visible or not, the end passed, no element manager), the
    destructors; TestInputComponent - 48 seeds: Create / Construct, bound to controllers, the handlers' code on
    random pad data (disabled / net-disabled / events off, network games with the blocking elements, buttons locked
    in the past / now / ahead / NaN), LockButton (times past 2^31), every member, the destructors.
  - Mutations: 71 deliberate bugs, all caught (66 at first; the test then varied the disable flags around the
    members, dirtied the buttons before LockButtonFromScript and gave clear= negative values and a presence bit of
    its own). The test also found the lock time's truncation (ours first rounded it).
