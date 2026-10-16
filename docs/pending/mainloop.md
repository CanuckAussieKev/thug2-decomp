- [x] MainLoop (NOT run in the game yet; pm/mainloop): `Mlp::Manager::MainLoop` 0x44EEF0 (src/game/sys/mlp.cpp,
      mlp.h), the game's main loop - the last but one of the PartyMod round's functions (WinMain is in pm/winmain).
      Sys::Main now calls it directly (no longer through the game's address).
      Convention: a plain thiscall (the manager in ECX, no arguments, plain RET) - no naked stub needed. Its only
      caller is Sys::Main (0x4E248D, replaced). EAX at its RETs is the entry value of m_done on one path and the quit
      flag's byte on the other: nobody reads it (register scan: nothing reads EAX/AL after it), so `void`.
      What it does, per frame (one pass of a do-while(!m_done), m_done cleared first and put back as it was on entry):
      the screen saver's timeout set again every 201 frames (SystemParametersInfoA GET/SET, restarts its idle time);
      the skip-render (0x7D1EC0) and 0x6F1158 flags cleared; keyboard read, window events (PartyMod's handleEvents),
      network service 0x536640; inactive window (0x7CCBE4) without text entry (0x7CCDF8): Sleep(100) + window events
      until active, then straight to the loop test (no frame time used); inactive with text entry: render skipped;
      quit flag (0x7D6A2C): out at once; device lost (0x786AB4): unless TestCooperativeLevel says still lost,
      release the default-pool objects (0x4E0320/0x4D67F0/0x4D7AF0), Reset, and on success restore them
      (0x4E0440/0x4D68C0/0x4D8420) and clear the flag - render skipped either way. Then the 60 Hz frame timing on
      QueryPerformanceCounter: wait (Sleep(0)) until a frame after the frame's start; more than 20 frames late:
      start again one frame back; more than 1.5 frames late (or the window inactive): render skipped, counted
      (0x696D70 in all, 0x696D74 in a row) - unless four in a row were skipped already and the window is active.
      The frame's start moves on by one frame. Then Tmr::CountVblank / OncePerRender; profiling (m_profileFrames
      +0xC4: "\nProfiling.....Start of main loop\n\n", count down, m_profiling +0xC8 for the frame); the system
      task list (+0x7C) then the logic list (+0x04) run with the profiling flag (0x4070A0); BeginScene 0x48BF10 and
      EndScene/Present 0x48BF20 unless the render is skipped (re-read after the tasks), around the frame's render
      0x48C330 and 0x565DA0; Tmr::UpdateFrameLength. The +0x40 list isn't run by the loop.
      PartyMod: its only patch inside is the call at 0x44F1C0 -> renderWorldWrapper; built in as
      RenderFrameLetterboxed (render, then the letterbox on in the front end - level byte 0 of the career, or no skate
      module - and off in a level), the original's plain call in a comment. HANDLED entry added in
      tools/check_partymod_overlap.py.
      Found by the test: the frame length is `frequency / 60` UNSIGNED (0x623D50 is _aulldiv; I first wrote a signed
      division - the original hung where ours didn't on a negative frequency).
      Test (src/hook/selftests_mainloop.cpp, TestMainLoop, Early): the original (PartyMod's renderWorldWrapper call
      written in at 0x44F1C0) and ours, with every callee stood in for by recording code - SystemParametersInfoA,
      QueryPerformanceFrequency/Counter and Sleep too (import slot and API), a stand-in D3D device (TestCooperativeLevel,
      Reset), PartyMod's setLetterbox - and a scripted clock; the loop ends the way the game ends it (m_done set by a
      task, the quit flag). Five cases: 450 jittered frames (waits, late, pauses, screen saver twice, profiling,
      tasks changing the skip flag, letterbox from 2 to 1); quit in frame 5 with a frequency over 2^32 in a level;
      device lost/reset failing/reset working plus the inactive window both ways; exact edges (1.5 frames, 20 frames);
      quit already set. Trace = every call with the frame number, the packed state and the profiling fields, the
      clock values read, the final state and a hash of the manager. Also checks ours calls RunStageOnce(EngineStarted)
      first (stood in, so the engine-started tests don't run offline).
      Mutations: 49 deliberate bugs, all caught (43 at first, the other 6 after adding the edges case, the
      inactive-with-six-in-a-row frame, an absolute clock in the device case and letterbox 2 in the front end).
      Not equivalent but not testable: none known. (A signed division would only differ for a negative frequency,
      which the edges/quit cases don't use - the hang showed it once; the code follows the original.)
      Shared harness: uses main's src/hook/standins.h (namespace TestStandIns, from pm/winmain; my own copy of the
      same extraction was dropped in the merge). TestMain also stands in for our MainLoop now (Sys::Main calls it
      directly).
      Test-runner mechanism: one flaw fixed in hook.cpp IsDisabled - disabling the game source in thug2-decomp.ini by
      address (0x0044EEF0) or by name ("MainLoop" is a substring of "MainLoopHook") also disabled the runner's
      fallback, so with MainLoop disabled the EngineStarted stage never ran. Fallbacks are now turned off only by
      `*`, their file (selftest_runner) or their exact name. (Offline this can't be exercised: thug2_check doesn't
      install hooks - check in the game: with `disable=0x0044EEF0` the log should still show the engine-started
      self-tests, run from MainLoopHook.) The mechanism otherwise looks right: a fallback is superseded only by an
      installable (not source-only, not disabled) game replacement; if that one then fails its argument-size check
      the game stops anyway (main's merge from pm/winmain now keeps the fallback in that case, so the stage and the
      stop message still come).
      Neighbours left (find_gaps; Mlp::Manager's other methods): 0x44EE90 (via the thunk 0x44F250), 0x44F220,
      0x44F240, and the singleton's create/release 0x44F260 / 0x44F2B0.
      Checks: builds without warnings; thug2_check 0 MISMATCHED, early 82/82; register scan only the known 0x4EFF92
      false alarm; call audit no MISMATCH (0x4D7AF0, 0x48BF10, 0x48BF20 "?" - they end in tail jumps to plain-RET
      functions, checked by hand); check_partymod_overlap "No overlaps". docs/PROGRESS.md not regenerated (merge).
