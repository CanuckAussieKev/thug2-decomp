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
- [ ] Next: the stream component (0x433870-0x433D90 and its member functions).
