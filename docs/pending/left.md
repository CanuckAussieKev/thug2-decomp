# wt/left: small leftovers and cleanups (NOT run in the game yet)

- [x] Front end's last pieces (src/game/front/front_end.*, 8 functions, 0x54EBE0-0x54EF30): ReleaseFrontEnd 0x54EBE0,
      DestroyBody 0x54EC20, PadsPluggedIn 0x54ECA0 (CFunc PadsPluggedIn), RemoveKeyboardHandler 0x54ECD0
      (StopKeyboardHandler), **AutoRepeat 0x54ECE0**, Destroy 0x54EE70, Start (v_start_cb) 0x54EEA0, Stop (v_stop_cb)
      0x54EF30. PadMapping's two unknown words are the auto-repeat state (0 / 1 waiting for the delay / 2 repeating)
      and its timer (ms, unsigned). Findings: held 0x8000/0x2000/0x1000/0x4000 = pad_left/right/up/down (that
      priority); controller 0 with the on-screen keyboard up also takes the first PC arrow key (0x14-0x17) without
      clearing the keys; the frame's ms = trunc(GetSlomoFrameSeconds * 1000); while the startup script runs AutoRepeat
      returns 0 for controller 0 and unpauses it each frame. Quirks kept: the destructor never deletes the keyboard
      task; Start puts each handler on its own index's controller (ignoring m_deviceServerMap). InputLogic, Sys::Main
      (Create/Release) and singleton.cpp (Spt::FrontEnd) call ours directly; the menu tests (selftests_menu.cpp)
      stand in for ours next to the game addresses. Tests: TestFrontEndAutoRepeat, TestFrontEndModule; 41
      mutations: 40 caught (37 at first), 1 equivalent (`elapsed < timer` vs `<=`).
- [x] Cutscene pieces (src/game/obj/cutscene.*, 10 functions): CutsceneNameFromPath 0x4638B0 (path in EDX: naked stub;
      no folder in the path: the static buffer 0x6F2160 keeps the old name), CCutsceneData::NameChecksum 0x463910
      (format "%s%d\n" - newline included), IsDone 0x4639A0, UpdateVblanks 0x4639C0, SetSubtitles 0x463A10, CheckSkip
      0x464120 (X 0x40 / Start 0x800 on the front end's first handler; global 0xA2107A0E blocks it), UpdateSubtitles
      0x4644A0 (quirk kept: a subtitle without a time leaves the local holding `this`); CCutsceneDetails (vtable
      0x647920) slots 9 0x464580 (IsSkippable), 11 0x463A50, 13 0x463A60. Names after THUG's Obj::CCutsceneDetails /
      CCutsceneData; CCutsceneData is ~0x34D0 bytes, only the fields used are laid out. Tests (selftests_left.cpp):
      TestCutsceneNameFromPath, TestCutsceneData (the game's sprintf stood in), TestCutsceneDetails; 48 mutations: 47
      caught (46 at first, 2 of them by a crash), 1 equivalent (NaN `<` vs `!>=`).
      **Left in the game** (the class is big, ~50 functions): 0x463AA0 (quaternion + translation to matrix, 7
      callers), 0x463BA0 (Male1/Female1 skater model name), 0x463C70, 0x463DA0, 0x463E80, 0x463ED0, 0x463F20, 0x463F70,
      0x464000, 0x4640D0, 0x4641A0, 0x464220, 0x464360, 0x4645A0 / 0x4645E0 (a pool allocator pair, pool 0x6FA7F4),
      0x464640-0x4680B0 (loading 0x466620, the details' slots 0 0x467F30, 2 0x467F60, 5 0x468040, 7 0x4680B0, 8
      0x466A10, the big updates 0x4648D0 / 0x465C00...).
- [x] core_physics.cpp: SelfEvent / BroadcastEvent called directly (CObject's); the skater tests stand in for ours too.
- [x] Cleanups: event.cpp uses `m_objectFlags & OBJECT_FLAG_SPAWN_HANDLERS`; tracker.cpp uses m_id, m_aliasCount
      (still compared signed: HasAliases), OBJECT_FLAG_HAS_POSITION, the position as the bytes after CObject
      (CCompositeObject's +0x4C). object.h: OBJECT_FLAG_HAS_POSITION = 0x20 and OBJECT_FLAG_SPAWN_HANDLERS = 0x10000
      added. cscript.h: CMemberFunctionEntry is 0x18 bytes {mName, mComponentType, mCode, mThisAdjustment,
      mVirtualBaseOffsets[2]} (unknown0 renamed mName; nothing used the old size). The tracker test doesn't cover a
      negative alias count (the signed compare).
- [x] Lst::Head::AddNode 0x45A540 (src/game/lst/head.*): objman.cpp calls it directly. TestHeadAddNode; 5 mutations
      caught.
- [x] CreateCompositeObjectManager 0x45F7A0 (compositemgr.cpp): singleton.cpp, Sys::Main and the misc test use ours.
      TestCreateCompositeObjectManager; 5 mutations caught.
- Not done: Tsk::Task 0x406890 / 0x406C90 - the whole task list area 0x406850-0x406E00 (~25 functions; 0x406C90 jumps
  to 0x406A80) is unrewritten and should go together.
