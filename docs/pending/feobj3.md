- [x] Batch (wt/feobj3): menu_backdrop front-end objects - the wheel group offset/shrunk (field of view) and the
      wheel scattering on the first spin; "Original behaviour:" comments at every hook (NOT run in the game yet).
      Record:

      ## 1. The group offset towards the screen centre and drawn smaller - the field of view
      - The placement kept each object's camera-space position, but the held LA view is drawn with the cameras'
        default angle (Nx::GetScreenAngle, 72 degrees at 4:3) while the menu camera used its own. From 10.png: the
        wheel centre lands at x 610 where the menu's text ring is centred at x 430 (2D layout 4:3 centred in 1920:
        240..1680). With the logged camera (59.8, -12.7, 181.7) and the wheel at (0, -12.75, -7) the wheel is ~17.6
        degrees off the view axis: at 72 degrees that is x ~ 645 (as seen), at the ring's 430 the menu camera's angle
        works out at ~47 degrees. So the menu camera is narrower; the same pose mapping then puts everything closer to
        the centre and smaller.
      - Where the angle comes from: the game gives the viewport camera its angle through Gfx::Camera::SetHFOV
        (callers 0x004A0AD0, 0x00467BA0, the skater camera 0x004F9AE0, the camera-animation code 0x00587A50 /
        0x00587F70 - PlaySkaterCamAnim's `fov` parameter, else a script float) or by writing it directly; HoldCamera
        then puts the default back. Now noted: MenuBackdrop::NoteCameraFov from SetHFOV (before HoldCamera), and at
        the renderer's hold point (NoteAndHoldCamera) whenever the camera's angle isn't the held view's; kept with the
        menu camera playing then; GameCameraFov(menuCamera) returns it (0: none noted). Forgotten with the pose.
      - Fix (keeps the user's LA framing - the held view's angle is unchanged): the placement scales the camera-space
        **depth** by k = tan(menuFov/2) / tan(viewFov/2) (FrontEndObjects::DepthScale): what the menu camera saw at
        (right x, up y, d ahead) goes to (x, y, k d) in front of the held view. On screen x / (k d tan(view/2)) =
        x / (d tan(menu/2)): the same place; an object of width w (not resized) at k d appears w / (d tan(menu/2)): the
        same size. Orientations only turn (rot); points use point = C^T diag(1, 1, k) V, back through its inverse. The
        menu camera's pose now carries both angles (equal twice in a row to be taken), and the log line says
        "field of view F (the view's V: depth x k)".
      - Worked: DepthScale(50, 72) = 0.641817, DepthScale(46.6, 72) = 0.592764; test points (10, 5, 100 ahead) and
        (-30, 12, 250 ahead) of the stand-in menu camera land at the same right / up and 0.641817 x the depth in front
        of views 1 and 2, with equal screen ratios (1e-4), and come back through the inverse.

      ## 2. The first spin scattering the wheel - rotations at the moved place
      - Obj_Rotate (spin_menu_spin_geometry) only sets the motion component's target; the turning happens inside each
        part's update. The previous code let the update run on the moved pose and then tried to work out the real pose
        from what changed ("changed near where we put it: kept, real pose worked back"). That is only right if every
        change commutes with the placement (a turn about the object's own axis); the scattering (pies seen edge on,
        bulbs thrown along an arc) shows the parts' turns didn't - turned about the wrong axis.
      - Fix: **each moved object is at its real place for its own update**. CCompositeObject::Update (composite.cpp)
        calls FrontEndObjects::BeforeUpdate once it isn't paused: what was done to the object since (outside its
        update - a spawned script's Obj_MoveToNode...) is taken in, then its real pose is written back (no teleport);
        its script, own update and components (motion's turning, Obj_MoveToNode, physics) run as in the original;
        AfterComponents takes wherever it is then as its real place and moves it into LA again (teleported: the model,
        shadow and collision follow). Nested updates (Obj_ForceUpdate) count; an update that ended early (the object
        died in its script) is noticed at the next step and the object moved from its real place. The suspend
        component's visibility pass (0x004342F0, from a walk over the suspend components and from 0x00564D30) runs
        outside the object updates, so it still sees the parts near the held camera. A skater whose physics is
        unpaused during its update is simply left at its real place.
      - The old "what changed" reconciling still handles writes made outside an object's update.

      ## 3. "Original behaviour:" comments
      At every menu_backdrop hook in rewritten game functions (render.cpp: Hidden, NoteAndHoldCamera, RenderWorld's
      Update; composite.cpp: BeforeUpdate / AfterComponents; compositemgr.cpp: Update; camera.cpp: SetPos / SetMatrix /
      SetHFOV; scene.cpp: LoadScene's OnLevelChange / OnSceneLoaded; cscript_exec.cpp: the two C function calls), a
      one-line "Original behaviour: ..." next to the "Not original" marker.

      ## Tests (src/hook/selftests_feobj.cpp; offline all 172 pass, 0 MISMATCHED)
      - TestFrontEndPlacement: + the depth-scaled placement (worked values above, both views, inverse, directions
        unchanged).
      - TestFrontEndObjectHooks: components now see the real place; a part turned during its update by 22.5 degrees
        twice **about its own z (Rz * m, the motion component's) and about the world's z (m * Rz)** ends up at the
        placement of the original turned the same way (matrix and display matrix, 2e-6) and is restored to it - the
        world-axis case fails with the old code (the hook removed: caught); nested updates; an update ended early; a
        position written from outside the update is where the next update starts.
      - TestFrontEndObjectsGameState: + the field of view: SetHFOV(50) while held noted (and the camera held at 72),
        another menu camera gets 0, the placement uses DepthScale(50, 72); written directly and noted at the hold
        point; kept when the camera is at the view's angle; forgotten; not noted when not held.
      - TestFrontEndObjectStates adjusted (outside-update checks through AfterComponents; the unpaused skater left at
        its real place by its update).
      - Mutations: 27 for the new code - all caught (2 after strengthening: the writes since taken in before the real
        place is written, the early-ended update); 1 equivalent: `samepose_no_fov` (the pose taken is the latest
        sample's, angle included). `fov_note_not_held` shows as a crash in the camera tests (a caught difference).
      - Register scan clean (only the known 0x004EFF92), call audit no mismatch, check_code_writes no overlaps.

      ## What the user should check (menu_backdrop = 1, debug_backdrop = 1 helps)
      - thug2-decomp.log: "the front-end objects placed for camera FCC018D3 at (...), field of view F (the view's 72:
        depth x k)". F should be the menu camera's angle (expected ~45-50). If it says 0.000 (not noted) or 72, the
        angle isn't the cause / isn't reached - send the line.
      - Main menu: the wheel centred in the menu's rotating text ring, the same size as in the shop's main menu, the
        skater's hand on its right edge.
      - Spin the menu up / down several times: the wheel turns as one piece (pies stay in the wheel's plane, bulbs on
        the rim), the skater plays his spin animations; no parts flying apart.
      - Options / network menus and back: the group re-placed for those cameras; CAS / skater select: the shop as
        before.
