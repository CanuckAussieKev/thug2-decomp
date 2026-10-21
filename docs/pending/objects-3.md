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
- [ ] Next: the shadow component (0x431300-0x431B10, 0x431360).
