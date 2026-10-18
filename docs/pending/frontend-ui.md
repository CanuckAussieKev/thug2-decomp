# frontend-ui - the front-end UI range 0x4A4500-0x4B2280 (screen elements & text)

Agent "frontend-ui" (branch worktree-agent-ab742dae3358eabeb). The user's request: finish the front-end UI range
(docs/subsystems.md "Front-end UI: screen elements & text") start to finish. Batch records below, in the style of
decomp-progress.md (the lead numbers them when merging).

- [x] Batch FE-1 (NOT run in the game yet; early test passes offline): the animation controllers (new
      src/game/gfx/anim_controller.*), 0x4A4500-0x4A5730 - the range starts with Gfx code, not the front end: the
      controllers an object's animation blender stacks (made by the factory 0x00481FB0 from a structure's Type with
      its Priority; the base controller 0x00481710 / vtable 0x0064877C isn't rewritten). Vtable slots: 0 scalar
      deleting destructor, 1 GetPose(pose: 64 rotations, 64 translations at +0x400, more at +0x800), 2 0x005BAB90,
      3 CallMemberFunction(name, params, script) -> 0/1/2, 4 InitFromStructure, 5 Update. Rewritten:
      - the standard controller (type 0xCB4533FF, vtable 0x00649F50, 0x28 bytes, its own item pools 0x00701430):
        operator new 0x4A4F30 (`(size, int)`, cdecl) / delete 0x4A5600 (Mem::PoolPair), Construct 0x4A4500,
        DestroyBody 0x4A4590, Destroy 0x4A55E0, GetPose 0x4A4F70, CallMemberFunction 0x4A4670 (PlayAnim 0xAF2FAE19:
        AnimName / StartTime / EndTime / LoopType / BlendPeriod / Speed on the channel's timeline (0x00481430), the
        frame from the frame source by anim set + name; 0xC68365EB: a new frame source for Anim; 0x92267217;
        InvalidateCache: refetch, then 2 = "not mine"), Update 0x4A4620 (the timeline steps (0x004814B0), its events
        between the two times (0x00481E10); the frame refetched when the time went back);
      - the weighted controller (type 0x7E1940A1, vtable 0x00649F68, 0x24 bytes; name not recovered): Construct
        0x4A5010 (`(timeline, object)`: the channel's object, or `object` without a timeline), DestroyBody
        0x4A4820, Destroy 0x4A50B0, GetPose 0x4A5660 (nothing with no frame / source or a weight <= 0 - NaN goes on;
        the skeleton component read through null without one, as the original), CallMemberFunction 0x4A4890;
      - the wobble controller (wobble, vtable 0x00649F80, 0x38 bytes): Construct 0x4A49F0, DestroyBody 0x4A4A80,
        Destroy 0x4A50E0, GetPose 0x4A5110, CallMemberFunction 0x4A4AF0 (PlayAnim, 0xD0209498: the target as a
        0..1 fraction 0x4D747FA0 of the timeline, SetWobbleDetails: all five of wobbleAmpA/B, wobbleK1/K2,
        spazFactor or nothing), WobbleTime 0x4A5180 (amplitudes grow by spazFactor towards the timeline's ends,
        the target kept clear of the ends, two sines of the game clock + 0x00696D60 (x87 FSIN, the cycles' integer
        part through _ftol), clamped to the timeline), Update 0x4A52E0;
      - the pose controller (pose, vtable 0x00649F98, 0x854 bytes): Construct 0x4A5730 (64 identity rotations; the
        rest left), DestroyBody 0x4A4D20, Destroy 0x4A5320, GetPose 0x4A4D30 (0x840 bytes copied);
      - the partial animation controller (partialAnim, vtable 0x00649FB0, 0x24 bytes): Construct 0x4A5350 (a
        timeline of its own: Calloc + 0x00481650), DestroyBody 0x4A4D50, Destroy 0x4A5400, GetPose 0x4A5430,
        InitFromStructure 0x4A4DD0 (base's, AnimName's times / loop type (0x004877E0 / 0x00487770), Speed (1),
        Current through the timeline's PlayAnim slot), CallMemberFunction 0x4A54A0 (0x6AAEB76F: one step of
        0x06C9F278 frames, the speed held at 0 meanwhile; 0xBD4EDD44 Speed (per 1/60 s); GetPartialAnimParams;
        0xF5E2B871: direction 0 <-> -1; InvalidateCache), Update 0x4A4EF0;
      - 0x4A4610 (slot 4 of four of them): a jump to the base's InitFromStructure.
      Findings: a missing PlayAnim AnimName leaves the original's argument slot (the params' address) as the name -
      kept; other missing values were uninitialised locals in the original (0 here). 32 functions.
      Early test (src/hook/selftests_fe_anim.cpp, with the shared src/hook/fe_testkit.h: arena pair, stand-in
      structures, float-returning stand-ins): every function, original vs ours, on stand-in controllers, timelines,
      objects, skeletons, frame sources and parameter structures; every external call recorded (frame sources,
      timeline pieces, anim set, memory manager and pools, structure getters / adders); the member functions'
      every case incl. missing parameters, NaN / equal / backwards updates, the wobble's clamps with positive and
      negative amplitudes, the pool paths of new / delete. 41 deliberate bugs caught (2 after strengthening: the
      wobble's clamps).
