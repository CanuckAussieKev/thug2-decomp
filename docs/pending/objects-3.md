# Object components, third part (0x42CECD-0x4351B2) - objects-3 agent (OC3)

Branch worktree-agent-af955367eb79d2242. The range's classes: the rigid body component (0x42CEE0-0x430E20 and its
member functions), a display-matrix component (0x1BBF844D), the shadow, skeleton, sound, static vehicle, stream,
suspend, 0x7505D7CC and trigger components, and two pieces of the vehicle component (its member function
registration, a wheel's settings). Ghidra misses ~20 of the range's functions (script member functions, the
components' RegisterMemberFunctions, a task) - found with capstone and the static initialisers that fill the member
function tables (0x00643530 rigid body, 0x00644760 skeleton, 0x006423F0 sound, 0x00644820 stream, 0x00644560 suspend).
Shared files touched: CMakeLists.txt (new files listed, additive); src/game/obj/trigger_component.h (the class's
other members added; TriggerNode unchanged).

- [x] Batch OC3-1 (NOT run in the game yet; the trigger component, the display matrix and static vehicle components,
  the vehicle's wheel settings, 19 functions): src/game/obj/trigger_component.* (extended),
  display_matrix_component.* / static_vehicle.* / vehicle_wheel.* (new).
  - Trigger component (vtable 0x00646648, 0x2C bytes: +0x18 where the object was at the last update, +0x28 the last
    trigger's events): Create 0x00434B90, DestroyBody 0x00434900, Destroy 0x00434910, InitFromStructure 0x00434940
    (the position now), Update 0x00434D90 (moved and not TELEPORTED: a feeler from the last position to the object's,
    SetIgnore(0, 0x50), movables too, the callback 0x00434D70 with the component at the feeler's +0xA8; NaN counts as
    moved), CallMemberFunction 0x00434970 (TriggerType: the unnamed integer array - or an array of them, any one -
    a subset of the last events; a non-integer array is 0; other names 2), CheckFeeler 0x00434BF0 (a trigger face
    (+0x6D) with a name (+0x58): through the tested movable's / the live hit object's nodearray component 0xC472ECC5
    (slot 13 gives its node array) with that object as `other`; a dead hit object: no nodes and the name the feeler
    cleared - 0), TriggerHits 0x00434CE0 (thiscall(events, hits): a list at +0x88 of 0x60-byte entries, +0xC30 the
    count - each trigger entry from the last, unless its name comes again later), FeelerHit 0x00434D70 (not a Ghidra
    function; cdecl callback: CheckFeeler with the zero events at 0x0064663C).
  - CDisplayMatrixComponent (type 0x1BBF844D, name unknown; vtable 0x00646464, 0x18 bytes): Create 0x004312A0,
    DestroyBody 0x00431240, Destroy 0x00431250, Update 0x00431280 (display matrix = the object's matrix).
  - CStaticVehicleComponent (0x9649D4F9 "staticvehicle", vtable 0x00646560, 0x58 bytes): Create 0x00433690,
    DestroyBody 0x00433650, Destroy 0x00433660, Finalize 0x004336F0 (the skeleton's bones from the model's hierarchy
    (0x00493040: 0x50-byte objects, matrix at +0x10): bone 0 zero, bone 1 the body at the root's translation, the
    rest wheels at objects 1-4; positions (-x, z, -y, 1). Not original: a 7th bone or more would read past the four
    wheels (the original's stack) - ours puts it at the origin).
  - Vehicle: RegisterVehicleMemberFunctions 0x00434F00 (not a Ghidra function; table 0x00684060), ReadWheelSettings
    0x00434F20 (stdcall(wheel, params), called by the vehicle at 0x004383D5 / 0x00438436: steering left/right/fixed
    -> +0xB0 1/2/0, drive yes/no -> +0xB4, spring_rate, damping_rate, radius, moment (stored 386.4 / moment),
    0xD87D0183, the frictions, the three grip velocities (mph -> inches a second, one x87 rounding), 0x3BCCBADC /
    0x9439D144 (feet -> inches); a steering= that isn't a name leaves the params pointer's value in the slot).
  - Test (selftests_obj_trigger.cpp TestTriggerComponent): 3000 random cases x the 20 functions, original vs ours,
    Calloc / the base component / Free / GetComponent / the nodearray slot / the feeler's GetCollision,
    GetTestedOwner, HitObjectAlive (clearing the name), GetHitObject / RemoveSmartPointer / TriggerNode / the struct
    getters / RegisterMemberFunctionTable / 0x00493040 / 0x004A2BA0 stood in for; the component, the allocation, the
    feeler, the wheel and the bones compared. 38 deliberate bugs caught, 7 after strengthening (only-w moves, event
    0, the wheel's whole record traced, a separate drive answer).
- [x] Batch OC3-2 (NOT run in the game yet; the shadow component, 15 functions): src/game/obj/shadow_component.*
  (new). CShadowComponent (0x8A897DD2 "shadow", vtable 0x00646498, 0x48 bytes: +0x18 the renderer's shadow object,
  +0x1C its position, +0x2C the ground normal, +0x3C ShadowType, +0x40 the model component, +0x44 follow the object):
  Create 0x00431800, Construct 0x00431380, DestroyBody 0x00431300, Destroy 0x004313D0, Finalize 0x00431400 (the model
  and its asset data to shadow slot 7), RefreshShadow 0x00431450 (visible = model flags 0x2 and not 0x4; detailed:
  SetPosition(pos + up * 36) - after an unused call of the model's +0x2C object's slot 5 (its vector * -1 dropped);
  simple: scale = model +0x1C then as Volume: Update(pos, matrix, normal); then Show - otherwise Hide), Hide
  0x00431570, CreateShadow 0x004315A0 (1 detailed 0x004A19C0 (Calloc 0x24), 2 simple 0x004A1750 / 0x004A17E0(name)
  (Calloc 0x14; written through without a check), 3 Volume 0x004A1F80 (Mem::New 0x68); 2 and 3 updated at once),
  DestroyShadow 0x00431780 (the model's shadow flag 0x004949B0(false) first), SetPosition 0x004317C0, SetNormal
  0x004317E0, SetLightDirection 0x00431360 (not a Ghidra function: shadow slot 3), InitFromStructure 0x00431860
  (ShadowType simple by default, the flag 0x7E773A11 = don't follow; simple: ShadowModel "Ped_Shadow", ShadowScale
  1), Update 0x004319D0 (following: the display matrix at the object's position, or the model hierarchy root's
  matrix x the display matrix at pos + its translation, to shadow slot 4; then RefreshShadow), CallMemberFunction
  0x00431B10 (Obj_ShadowOff / Obj_ShadowOn: ShadowType simple added when missing, InitFromStructure through the
  vtable, a simple shadow's offset= 0.2 to its +0x10).
  - Test (selftests_obj_shadow.cpp TestShadowComponent): 4000 random cases over the 15, the shadow object's 8 slots and
    the fader's slot 5 recording vtables, the three constructors, the allocators (Calloc and the manager's Allocate),
    the model's shadow flag / hierarchy, the struct getters stood in for; the component and both shadow objects
    compared. 35 deliberate bugs caught (the two crashes: a shadow updated through null).
- [x] Batch OC3-3 (NOT run in the game yet; the skeleton component, 15 functions): src/game/obj/skeleton_component.*
  (new). CSkeletonComponent (0x222756D5 "skeleton", vtable 0x006464DC, 0x1C bytes: +0x18 the renderer's skeleton):
  Create 0x00431F60, RegisterMemberFunctions 0x00431BE0 (not a Ghidra function; table 0x00684DE0, filled by
  0x00644760), DestroyBody 0x00431FE0 (0x004A2CB0 + Free), Destroy 0x00432790, InitSkeleton 0x00431C70 (asset data ->
  0x004A3D30 on Mem::New(0x1C)), InitFromStructure 0x00431CF0 (CopySkeletonFrom=<id>: 0x004A3E80 copy of that
  object's skeleton; else skeleton= / skeletonName= (asserted; neither: the params pointer's value), and
  max_bone_skip_lod= to the skeleton's +0x10), GetLocalBonePosition 0x00431C60 (a jump to the skeleton's 0x004A3870),
  GetBonePosition 0x00432050 (the bone's position - (0,0,0,1) if not found - through the object's matrix at its
  position; ALWAYS returns false: the original clears AL), SpawnTextureSplat 0x004320F0 (RET 0x38: the start from
  the bone or `start`; dropdown: the end straight down by dropdown_length feet; else `end`, or - when its length is 0
  - that far down the object's up; the start then raised a foot up the up; the end scattered by two rand()s along
  right / at; x87 detail kept: several products unrounded (x, and x / y of the raise), the second scatter offset
  unrounded; 0x004928A0 with the bone when `trail`), SpawnCompositeObjectAtBone 0x00432490 (RET 0x1C: the bone's
  matrix (0x004A36E0) with rows 1 / 2 swapped and the new row 2 negated, moved by the offset (whose w is cleared -
  the caller's vector), times the object's matrix at its position (rows' w cleared) by D3DX; pos, Orientation (the
  quaternion 0x0042E1F0, negated unless w > 0), vel (scale * object vel + vel turned) and rotvel (turned) added to
  `params`; 0x0045F3A0 with finalize). Members (table): Obj_SetBoneActive 0x00431C00 (not a Ghidra function; bone
  missing: the component's address - the slot PUSH ECX made; active missing: the params pointer), Obj_SetBoneScale
  0x00431E30 (propagate -> 0x004A2D80's last argument = !propagate; propagate_reverse: every bone from 1 not under it
  and not it, flag 0x8000000, force_update -> 0x004A4000), obj_GetBonePosition 0x004327C0 (x, y, z added to the
  script's params; returns GetBonePosition's false), TextureSplat 0x00432860 (size, 0x3786B657 (height?, size by
  default), radius (spread), lifetime 5, dropdown_length 3, 0x78AAF97F degrees 180 (turn), blendmode (blend), name= /
  texture= as string (0x0049F560) or checksum (0x0049F540), bone (or forward= along the at), end_pos, spin =
  rand()*2pi/32767 or - with 0x744E673F - acos(at.z) signed by at.x < 0, trail, dropdown_vertical),
  Skeleton_SpawnCompositeObject 0x00432B90 (not a Ghidra function). Not original: TextureSplat's size starts at 0 and
  its start at the object's position when there's neither bone nor forward= (the original's are uninitialised stack);
  the asserted values of the member functions start at 0.
  - Test (selftests_obj_skeleton.cpp TestSkeletonComponent): 4000 random cases over the 15 (and SpawnTextureSplat /
    SpawnCompositeObjectAtBone called directly with every flag), the skeleton's functions, rand(), the splat, D3DX
    (sometimes a half turn: the quaternion's w = 0), the struct getters / adders, the manager stood in for; values
    with full mantissas so x87 roundings show. 44 deliberate bugs caught, 6 after strengthening (full-mantissa
    values, the half turn, the offset checked); 1 equivalent (Create's explicit null skeleton: Calloc zeroes).
- [x] Batch OC3-4 (NOT run in the game yet; the sound component, 18 functions): src/game/obj/sound_component.* (new).
  CSoundComponent (0x07713C7B "sound", vtable 0x00646514, 0xC0 bytes: +0x18 eight sound slots (Obj_SetSound - type
  8 is accepted and lands on the next field), +0x38 two waiting looping sounds (PlaybackProps + the time to look
  again), +0x98 position, +0xA8 a frame ago, +0xB8 emitter node, +0xBC proxim node; the sfx manager takes the
  component as its "object" - sfx_voices.cpp's calls to 0x00432E50 / 0x00432EB0 / 0x00432F10 / 0x00432D40 /
  0x00432F70 are these): Create 0x00433100, RegisterMemberFunctions 0x00432C80 (not a Ghidra function; table
  0x00683118), Construct 0x00432D90, DestroyBody 0x00432DF0 (StopObjectSounds), Destroy 0x00433160,
  InitFromStructure 0x00432CA0 (ProximNode=), Update 0x004335D0, CallMemberFunction 0x00433190 (Obj_AdjustSound,
  Obj_StopSound - also drops a waiting one -, Obj_SetSound), KeepLoopingSound 0x00432D40 (2 s later),
  GetSoundPosition 0x00432E50 / GetPreviousSoundPosition 0x00432EB0 (the emitter's nearest point to the camera),
  GetSoundExtent 0x00432F10 (the proxim node's), UpdateProps 0x00432F70 (`this` unused: pitch / volume towards
  +0x18 / +0x1C by the steps +0x20 / +0x24 times 60 x the frame's seconds, Mth::KeepWithin), UpdateWaitingSounds
  0x00433480 (due ones: played - forced - when the nearest camera is within dropoff (0: 1020) + 240 (the sum in
  double), then faded in from 0 at volume3 / 100 a frame; else looked at again at now - ftol((distance - dropoff) *
  1000 * -1/600) + 333 ms), PlaySound 0x00433240 (Obj_PlaySound's body: vol / pitch or Percent between min / max,
  dropoff feet, dropoff_function, NoPosUpdate (not positional), emitter; the slot `Type` when no checksum; vtable
  slot 13 (empty 0x0048D090) told first; PlayObjectSound with {pitch, volume, dropoff, falloff}), AdjustSound
  0x00432FF0 (volumePercent / pitchPercent of volume2 / pitch2, at once unless volumeStep / pitchStep), SetSound
  0x00432CE0, Obj_PlaySound 0x00433630 (not a Ghidra function).
  - Test (selftests_obj_sound.cpp TestSoundComponent): 5000 random cases over the 18, the sfx manager's four, the
    cameras, emitter / proxim nodes, the clock (GetSlomoFrameSeconds' double through a naked stand-in), the struct
    getters stood in for; edges: a sound due exactly now, the camera exactly at the edge of hearing (the float sum
    rounding below the double one). 42 deliberate bugs caught, 3 after strengthening; 1 equivalent (Construct's
    explicit null emitter: Calloc zeroes).
- [x] Batch OC3-5 (NOT run in the game yet; the stream component, 15 functions): src/game/obj/stream_component.*
  (new). CStreamComponent (0x0F1641E3 "stream", vtable 0x00646594, 0x2C bytes: +0x18 three stream ids, +0x24
  emitter node, +0x28 proxim node; Pcm's stream_commands.cpp calls 0x00433CD0 / 0x00433D30): Create 0x00433D90,
  RegisterMemberFunctions 0x00433870 (not a Ghidra function; table 0x00684E70), Construct 0x00433C10, DestroyBody
  0x00433890 (Pcm::StopStream of each), Destroy 0x00433C40, InitFromStructure 0x00433900 (ProximNode=),
  GetStreamPosition 0x00433CD0 (the emitter's nearest point with a camera, else the object's position),
  GetStreamExtent 0x00433D30, PlayStream 0x00433970 (only with a free place: vol / pitch 100, priority 50,
  num_loops, id, dropoff feet, dropoff_function, emitter, the unnamed stream, use_pos_info 1 ->
  Pcm::PlayStreamFromObject; whether it played), AddStream 0x00433B30 (Add=), RemoveStream 0x00433BA0 (remove=), and
  the members (not Ghidra functions; all true): Obj_PlayStream 0x00433C70, Obj_AddStream 0x00433C90,
  Obj_RemoveStream 0x00433CB0, Obj_StopStream 0x00433930.
  - Test (selftests_obj_stream.cpp TestStreamComponent): 4000 random cases over the 15, Pcm's four, the nodes, the
    camera, the getters stood in for. 21 deliberate bugs caught, 1 after strengthening (Create's record traced before
    Construct overwrote it).
- [x] Batch OC3-6 (NOT run in the game yet; the suspend component and its task, the texture projection component,
  Mth::Vector::MultiplyBy, 26 functions): src/game/obj/suspend_component.*, texture_projection_component.*,
  src/game/mth/vector_transform.cpp (new); vector.h: MultiplyBy declared (additive) with a forward declaration of
  Matrix.
  - CSuspendComponent (0xCE0CA665 "suspend", vtable 0x006465C8, 0x4C bytes: +0x18 NeverSuspend, +0x1C / +0x20 the
    suspend distance (inches) and its square, +0x24 the distance squared at the last check, +0x28 frames before the
    first check, +0x2C the model component, +0x30 far (suspended), +0x34 lod_dist1-4 (inches; 1000000-1000003 = no
    LOD), +0x44 frames before the LOD checks): Create 0x00434290, RegisterMemberFunctions 0x00434610 (not a Ghidra
    function; table 0x00684C48, then a tail jump to CreateSuspendTask), Construct 0x00433F50 (NeverSuspend by default,
    DefaultMovingObjectSuspendDistance), DestroyBody 0x00433E10, Destroy 0x00433FD0, Update 0x00433E50 (suspends
    itself: the base's Suspend(true)), InitFromStructure 0x00434000 (SuspendDistance - 0: lod_dist1, else the default -
    clears NeverSuspend; NoLOD script integer; lod_dist1 50, lod_dist2 = lod_dist1 + 75, 3 / 4 1000001 / 1000002, x12,
    the first two times LOD0 / LOD1DistanceMultiplier; the order checks call 0x005480A0(params, 0): kept as
    Script::PrintContents), Finalize 0x004341C0, ResetCounts 0x00433E60, DisableLod 0x00433E70, IsFar 0x00433E90
    (REPLACE_SOURCE_ONLY: 4 bytes), ShouldAnimate 0x00433EA0 (beyond lod_dist1 never - NaN counts as within -;
    AnimLODInterleave4 (unrounded square) every 4th by 0x006951B8, AnimLODInterleave2 (square rounded to float) every
    2nd by 0x006951B4; always during the LOD frames), UpdateVisibility 0x004342F0 (the MODEL COMPONENT's +0x234 forces
    it; within lod_dist2 strictly: the model's bounding sphere 0x00492F30 through MultiplyBy + the position to
    0x0048C490 (nonzero: out of view); 0x00493C60(visible); newly visible: the model component's Update),
    Obj_ResetNoSuspendCount 0x004341E0 (not a Ghidra function). NextSuspendFrame 0x00433E20 (the counters
    0x006951B0-B8), CreateSuspendTask 0x004345B0 (holder at 0x006951BC), SuspendTaskHolder::Construct 0x00434520
    (Tsk::Task 0x20 bytes, vtable 0x006476F0, priority -1100, added to the main loop's logic tasks - null too),
    SuspendTask 0x00434420 (not a Ghidra function; quirk kept: a component's check frames are counted down all at
    once - the original goes round with the same component; camera = the skate module's +0x3DC (flag, position);
    distance squared in double against the square; Suspend / wake; the model shown during the LOD frames, else
    UpdateVisibility).
  - CTextureProjectionComponent (0x7505D7CC - a name for "texture_projections="; vtable 0x00646604, 0x38 bytes: +0x18
    the list head (vtable 0x00646638 made, 0x00646600 in its destructor), +0x2C / +0x30 / +0x34 the suspend, skeleton
    and model components): Create 0x00434870, ProjectionList::Destroy 0x00434670, DestroyBody 0x004346B0 (0x0057A2F0
    then the head unlinked), Destroy 0x00434840, InitFromStructure 0x00434730 (each texture_projections= structure: a
    0xC8-byte projection (0x004C7E80) with its 0x1EE8B7B9 checksum at +0x14, 0x004A04C0(structure), at the front),
    Finalize 0x00434630, Update 0x00434810 (each projection's 0x0049FFE0(object); the head test before the null test).
  - Mth::Vector::MultiplyBy 0x00434200 (14 callers: v = v * M over all four, the sums in the original's order in
    double).
  - Test (selftests_obj_suspend.cpp TestSuspendComponent): 5000 random cases over the 26 on chains of four components;
    Script::GetFloat through a naked stand-in (the float in ST0); edges: distances exactly on the LOD / suspend limits,
    at AnimLODInterleave2's float square, and a crafted vector / matrix that puts MultiplyBy's sum on a float tie (the
    order of its two last additions shows). 44 deliberate bugs caught, 6 after strengthening (a hash collision
    between two answers, the edges, the tie).
- [x] Batch OC3-7 (NOT run in the game yet; the rigid body component's life, shapes, set-up and member functions,
  Mth::Quat, 25 functions): src/game/obj/rigidbody_component.*, src/game/mth/quat.* (new); src/game/mth/random.*:
  PlusOrMinusExact added (0x00403E80's result unrounded, as the x87 callers use it) and PlusOrMinus now its rounding
  (it rounded each step before).
  - CRigidBodyComponent (0xFFB39248 "rigidbody", vtable 0x006463F8, 0x1A0 bytes; the layout in the header: constants
    +0x18..+0x5C, the contacts +0x60 / capsule +0x64 / directed friction +0x88 / count +0x8C, spheres +0x90 / +0x94,
    springs +0x98 / +0x9C, centre of mass, model offset, scripts, sound structures, flags +0xF4, state +0x10C, pos /
    vel / quaternion / rotvel / matrix +0x110..+0x18F, +0x190 contacts hit, +0x198 radius): Create 0x0042EF00,
    Construct 0x0042E3F0 (the assent's cos / sin of the double 0.43633 by the x87's FCOS / FSIN - inline asm), DestroyBody
    0x0042EF60, Destroy 0x00430DF0, InitFromStructure 0x0042F050 (its jump table at 0x00430124: Box / Pyramid /
    Cylinder / Triangle; contacts= or the spheres skip it; the rest as the header says; x87 detail kept throughout:
    sums in double, the Angles turn's terms in the original's two orders (contacts / spheres), lengths compared
    unrounded for the radius and rounded for the extent, the offset's x / y unrounded), SetContacts 0x0042D4B0, SetBox
    0x0042D560, SetPyramid 0x0042D690, SetCylinder 0x0042D790 (cosine of the unrounded angle, sine of the angle rounded
    to a float - FCOS / FSIN), SetTriangle 0x0042D8A0, SetSpheres 0x0042ED60, SetSounds 0x0042EB10 (RigidBodySounds),
    members (table 0x00683F28; 0x0042BB60 / 0x0042BBB0 / 0x0042BBD0 are the neighbouring range's): RigidBody_Wake
    0x0042DCA0, RigidBody_Sleep 0x0042DCF0 (0x0042BE90), Rigidbody_Kick 0x0042DD00, RigidBody_SetVelocities
    0x0042DEB0, RigidBody_Enable 0x0042EE30, RigidBody_Disable / EnablePlayerCollision 0x0042DFB0 / 0x0042DFC0,
    [0xD4A0946B] CenterOfMassPos 0x0042DFD0, Rigidbody_MatchVelocityTo 0x0042E020 (the first five are not Ghidra
    functions), AddRigidBodyPusher 0x0042E380 (the 8 smart pointers at 0x00695150 - skater.cpp's caller).
  - Mth::Quat::FromMatrix 0x0042E1F0 (the trace branch's 0.5/s rounded to a float, the other branch's not) and
    Normalize 0x0042E310.
  - Not original: none. Quirks kept: CallbackParams' old copy leaked, a missing sound type / failed allocation
    written through null.
  - Test (selftests_obj_rigidbody.cpp TestRigidBodySetup): 4000 random cases over the 25 (allocations from a stand-in
    heap - traced -, the struct copies and deletes, the script getters, 0x0041A730, GetAngles, smart pointers stood
    in for; PlusOrMinus's stream reset per case); edges: crafted points and a crafted turn on a float tie (the order of
    the Angles sums shows), +-30 points (the size limit), a not-a-number point looked at last, a tie for the largest
    diagonal, slow objects for MatchVelocityTo. 50 deliberate bugs caught, 11 after strengthening; 1 equivalent (the
    radius compared rounded: an equal float is stored either way). Builds: this worktree's verify build switched to
    embedded debug info (CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded, out\scripts\reconfig.cmd): the shared vc140.pdb
    kept breaking during mutation runs (C1033 / C2471).
- [x] Batch OC3-8 (NOT run in the game yet; the rigid body component's physics, 8 functions):
  src/game/obj/rigidbody_physics.cpp (new); rigidbody_component.h (the physics' declarations, the world contact
  record, names for +0x38 / +0x3C (damping), +0xC0 / +0x194 (the push cooldown), +0xD0 (the stuck script), +0x19C
  (the sound component)); gfx/debug_lines.h: AddDebugStar / AddDebugSphere (compiled out, as AddDebugLine).
  - CRigidBodyComponent::Update 0x00430E20 (vtable slot 1: the push cooldown or the pushers 0x0042C140, the die
    timer (slot 6 MarkAsDead when it runs out), asleep: only the display matrix; awake: up to 5 steps, each a feeler
    line along the step's path - a hit cuts the step to 6 units before it (not below 0) and collides; the fifth step
    puts it to sleep (0x0042BE90); the contacts (0x0042C950), springs, damping 0x0042C6D0, sleep check 0x0042C890; the
    object's position by the turned model offset, matrices, velocity), Integrate 0x0042E540 (gravity's half step left
    out when +0x191 >= 3; the orientation turned by q * (-rotvel / 2) dt through 0x00420140 and normalised, its matrix
    0x0042BC40, the world contact / sphere / spring points - each set's sums in its own order), Collide 0x00430BE0
    (the bounce by 1 + restitution, friction across the normal capped by the bounce's length times the friction),
    ResolveContacts 0x00430140 (impulses up to 20 rounds - one contact: one round; stuck: the +0xD0 script, else the
    velocities zeroed with w = 1; friction from the velocities before the impulses, along the turned directed
    friction where active; the depth springs; OnBounce at the last active contact with the fastest approach),
    ApplySprings 0x0042CF90 (the spring points' velocities taken out, up to 20 rounds, one spring: one round; then each
    pulled to its anchor by spring_const), ImpulseScale 0x0042CEE0 (thiscall, the result unrounded in ST0), OnBounce
    0x0042E980 (BounceScript with CallbackParams and the point 0x43F454D0; the bounce sound at Percent (clamped 0-100)
    while more than 12 of 48 voices are free), DebugDraw 0x0042D9D0 (the drawing calls compiled out of this build; the
    switches 0x006939D4 / 0x006939D5 kept).
  - x87 detail kept throughout: what stays on the FPU stack a double, each store a float rounding, the original's
    sum orders (the turned points' three orders, ImpulseScale's x rounded / y, z not, its inlined copy in
    ApplySprings with the normal's x unrounded). Quirks kept: CallbackParams appended through a null structure when
    the allocation fails.
  - Test (selftests_obj_rigidbody_physics.cpp TestRigidBodyPhysics): 6000 random cases over the 8 (the feeler,
    pushers, contacts finder, sleep / damping, scripts, structures, voices and sound stood in for; the quaternion
    helpers run for real; the world arrays 0x006949B0-0x0069514F traced); edges: crafted points on a float tie (the
    turned sums' order), speeds exactly at the spring / friction limits, a die timer running out exactly, a point
    exactly at its anchor, directed friction along the normal, one contact / spring. 66 deliberate bugs caught (5
    after fixing mutations that didn't build).
- Range complete: inspect survey 0x42CECD-0x4351B2 shows all 120 Ghidra functions REPLACED; find_gaps.py: no gap
  in the range.
- [x] OC3 game run (2026-10-22, workspace C:\Games\THUG2-3, this branch merged with main 6e21460, OC3-1..8): `-Level BO`,
  teleported next to BO_Bouncy_Ball01 (a rigid body; its Update runs every frame), walked past it twice (posted `up`
  presses), ~60 s: the level loads, the skater spawns and walks, the range's replacements installed, no exception /
  run-time check / CRT report in the log. Not driven: a bail (the bail board is a rigid body - bails.qb
  BailBoardComponents), thrown projectiles (projectiles.qb), make_skater_explode's body chunks (skater_fx.qb), kicking
  a bouncy object hard. thug2_check after the merge: 0 MISMATCHED, 635 early self-tests pass on THUG2PM.exe and the
  stock THUG2.exe; check_code_writes: no overlaps.
- For the user to try: in Boston kick the bouncy balls / trash cans (BO_Bouncy_*) and watch them roll, bounce and
  settle; bail (the board flies off, tumbles and settles); throw projectiles where the game allows. Off switches
  (`[replacements] disable=`): rigidbody_physics.cpp (the physics only), rigidbody_component.cpp, and the other files
  of the range: trigger_component.cpp, shadow_component.cpp, skeleton_component.cpp, sound_component.cpp,
  stream_component.cpp, suspend_component.cpp, texture_projection_component.cpp, quat.cpp.
