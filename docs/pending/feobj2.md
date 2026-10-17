- [x] Batch (wt/feobj2): menu_backdrop front-end objects - fix for "LA shows, but no wheel and no skater" (batch 93),
      and [features] debug_backdrop (NOT run in the game yet). Record:

      ## What the game's log showed
      The backdrop loaded (one "loading the scenes LA_Sky and LA" line after start-up) but no "the front-end objects
      placed for camera ..." line: the menu camera's pose was never captured, so nothing was ever moved. (The user saw
      LA, so the held view itself - setting, level 0, LA loaded, a menu camera playing - was on in the renderer.)

      ## The cause (from reading the code paths; not proven in the game - debug_backdrop will show it)
      - **Not the hold-off.** The suspected nested LoadScene sequence is fine: load_level's LoadScene "mainmenu" (its
        start: OnLevelChange -> held off) -> MenuBackdrop::OnSceneLoaded -> LoadScene "LA_Sky", LoadScene "LA" (each
        held off again) -> then, after both, OnBackdropLoaded (no longer held off). level_mainmenu has no sky and no
        outer shell, so no LoadScene follows. A new test runs exactly this (the real CFunc_LoadScene, its callees
        stood in) and it passes on the old code too.
      - **The capture depended on two things the game may not give:**
        1. Samples were taken **only by the composite object manager's update walk** (Update(true)); the renderer's
           step never sampled. Anything that keeps the walk's step from sampling while the menu camera plays (its
           task not running in the front end, or running before the camera's animation starts every frame...)
           means no capture, ever.
        2. The pose came **only from the camera object** owning the viewport camera's camera component (found
           through the composite manager's per-type component list). If the viewport camera isn't owned by such a
           component while a skater camera animation plays, there was no sample and the sample count was reset on
           every step - no capture, not even the 30-sample fallback, and no log line. This matches the log.
      ## The fix
      - The camera is sampled **once a frame by whichever step comes first** - the object walk's or the render's
        (frames counted by the renders; Step now returns whether it took a sample).
      - When no camera object is found, the pose comes from **what the game gave the viewport camera's setters**:
        Gfx::Camera::SetPos / SetMatrix (camera.cpp) now call MenuBackdrop::NoteCameraPos / NoteCameraMatrix before
        HoldCamera, which keep the position and matrix given to the viewport camera while it is held (before the view
        replaces them). Both must have been noted; forgotten when the backdrop is loaded again.
      - [features] **debug_backdrop** = 1 (default 0; FrontEndObjects::DebugBackdrop()): logs every change of the
        decision's inputs ("menu_backdrop debug: setting 1, level 0 1, LA loaded 1, menu camera FCC018D3, held off 0,
        applied 0, pose known 0 (0 known), camera from its object | the setters | nowhere, N sample(s) in a row,
        sampling | non-sampling step, N moved"), each hold-off / backdrop reload, "applying for camera X", each object
        moved (id, from, to), restored (physics unpaused) or forgotten, and every restore with its reason (setting off,
        not the front end, backdrop not loaded, held off, not a menu camera, another menu camera, another view, camera
        pose forgotten, level change). CLAUDE.md lists the setting.

      ## Tests (src/hook/selftests_feobj.cpp; offline all 172 pass, 0 MISMATCHED)
      - New TestFrontEndObjectsLoadSequence: the front end's load as the game does it - CFunc_LoadScene("mainmenu")
        (ours; structures, texture dictionary and scene file stood in, each loaded scene put in a stand-in scene list),
        which loads LA_Sky and LA through LoadScene - then frames with mainmenu_camera02: run once with walks and
        renders, once with **renders only** (debug_backdrop on). The renders-only run **fails on the old code** (no
        sample was ever taken) and passes now; the scenes loaded are mainmenu, LA_Sky, LA, and it isn't held off.
      - TestFrontEndObjectsGameState extended: sampling once a frame (two walk steps in a frame count once; the
        render's step samples too), renders only, and the setters' pose (no camera component: nothing until both
        position and matrix are noted, a position alone isn't a pose, then moved with that pose; another camera than
        the viewport's, or not held: not noted; forgotten when the backdrop loads). These **fail on the old code**.
      - Mutations: 15 for the new code - all caught (one after strengthening: a position alone isn't a pose; one
        equivalent: `input_pose_matrix_only` - Update always sets both pointers or neither; `note_matrix_not_held`
        shows as a crash in the camera tests, a caught difference).
      - Register scan clean (only the known 0x004EFF92), call audit no mismatch, check_code_writes no overlaps.

      ## What to check in the game
      - With the defaults: the main menu shows the wheel and the skater in LA; thug2-decomp.log has "menu_backdrop:
        the front-end objects placed for camera FCC018D3 at (...)".
      - If not: set `[features] debug_backdrop=1`, start the game to the main menu, quit, and send the "menu_backdrop
        debug" lines: they show which input stays wrong (e.g. "camera from nowhere" = neither source gives the
        camera's pose; "menu camera 00000000" in the sampling step = no menu camera seen; "held off 1").
