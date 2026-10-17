- [x] Batch (wt/feobj): menu_backdrop - the main menu's wheel and skater moved into the backdrop (NOT run in the game
      yet). Record:
      At the user's request (after testing batch 91: with the backdrop on, the spinning wheel and the skater who spins
      it were gone - they stay at the skate shop, ~6,300 units from the held view in Los Angeles). Part of the
      menu_backdrop feature, marked "NOT ORIGINAL": src/game/front/frontend_objects.{h,cpp}. With `menu_backdrop=0`
      nothing is moved and every hook returns at once.

      ## What the wheel and the skater are (mainmenu.qb / mainmenu_scripts.qb, unpacked from the .prx archives)
      - **The wheel of loogies** ("wol"): 48 **level objects** of mainmenu.qb's node array (`Class = LevelObject`,
        CreatedAtStart, SuspendDistance 0, lod_dist1 200 / lod_dist2 201): wol_inner_ring, wol_inner_star, wol_tag,
        wol_outer_ring, wol_pie_1..16, wol_lightbulb_1..28, all near the shop's origin ((0, -12.7, -7) etc.). Each is
        a composite object (gameobj components: suspend, motion, objectproximity, sound, + a model because of
        `Class = LevelObject`). The model component's InitFromStructure (0x00420D10) for a level object: 0x0041FFA0
        finds the sector of that name in **any** loaded scene (0x0048C510) and clones its geometry as an **instance**
        (geom vtable +8 with instance = 1); the model component's Update (0x004219F0 -> 0x004215F0) draws it with
        the object's display matrix and position. Instances are drawn by the renderer's instance passes (0x004D1760 ->
        CInstance::Render 0x004D1130, from SetPass), **not** by the scene loops menu_backdrop hides - so hiding the
        "mainmenu" scene doesn't hide them; they were invisible only because they are at the shop. Turned by
        `spin_menu_spin_geometry` (`Obj_Rotate absolute = (0,0,1)*angle time = ...` on each part: the motion
        component's rotation, 0x00425A80 -> 0x004258B0, multiplies the matrix by a rotation about the object's
        **own** axis - it commutes with moving the object), shown / hidden by `spin_menu_visible` (Hide / Unhide).
      - **Visibility of level objects**: the suspend component (0x00433F50; its model at +0x2C) decides every frame
        whether the model is drawn (0x004342F0: off beyond lod_dist2 (201 ft) **from the camera**, else a frustum
        test 0x0048C490) - with the camera held in LA the shop's parts were out of range too. So the objects must
        really be moved (in the logic, not only when drawn): then distance and frustum are right.
      - **The skater**: the local skater (FindObject("Skater")), placed by `SpinMenuSkaterIdle` (MakeSkaterGoto from
        spin_menu_skater_script): SkaterInit, `Obj_MoveToNode name = trg_playerrestart Orient NoReset` ((84.8, -63.7,
        61.7)), SetRollingFriction 10000, SetSpeed 0, **PausePhysics**, then animations only (FrontEnd_Wheel_Idle /
        Spin / Spin2Wheel...). PausePhysics is the skater physics control component's (type skaterphysicscontrol
        0xD9151D4D, vtable 0x0064BFE0; member functions 0x00517A80 / 0x00517AA0, table 0x006846F0): its byte +0x18
        = paused; while paused its Update (0x00517AC0) only copies a matrix into the display matrix. The options menu
        (in_options_menu, `ShadowSkaterOptionsTricks`, `ShadowSkaterFreestyleAI`) runs the skater **with physics**
        (SetSpeed 1, Jump...) on the shop's collision - it must not be moved then (LA has no collision loaded: it
        would fall).
      - **The camera**: the main menu plays `mainmenu_camera02` on skatercam0 with `targetid` = the skater
        (spin_menu_camera_init); the camera component (0x0041B840: CCameraComponent, vtable 0x00645E34, its
        Gfx::Camera at +0x18) copies its object's m_pos / m_matrix into the Gfx::Camera (0x0041B810 / 0x0041B8A0)
        - so the camera object still holds the original menu camera's pose after HoldCamera replaced the
        Gfx::Camera's.

      ## The framework (frontend_objects.*)
      - Table (kObjects): {name (with %d for numbered ones), count, kind, offset}: "Skater" (kind Skater: moved only
        while its physics is paused) and the 48 wol_ parts. Offsets (default 0) are in the backdrop view's frame
        (right, up, back) for fine-tuning.
      - Placement: the rigid transform taking the menu camera to the backdrop view: rot = C^T V (C, V the camera's
        and the view's frames: row 2 = back, row 0 = up x back, row 1 = back x row 0, orthonormalised), trans = view
        pos - camera pos * rot (+ offset). Applied to position, old position (points), velocity, orientation rows 0-2
        and display matrix rows 0-2 (directions); every w and row 3 pass through. In double, rounded once.
      - The menu camera's pose: read from the object owning the viewport camera's camera component, while nothing is
        moved; taken when the same for two logic updates in a row (or after 30 samples if it keeps moving), kept per
        camera animation (mainmenu_camera02 / 03, SS_MenuCam, level_select_cam...) until the front end / backdrop goes.
        Each menu camera thus gets its own placement (objects keep their place for whichever menu camera plays).
      - Moving = the fields written, then CCompositeObject::SetTeleported(true) (each component's Teleport: the model,
        shadow, sound and collision components' Teleport is their Update - the drawn instance follows at once).
      - When: MenuBackdrop's view set, career level 0, the backdrop's scene loaded, a menu camera playing, not held
        off, the camera pose known. Checked at the start of every object walk (CCompositeObjectManager::Update, which
        also samples the camera) and of every render (RenderWorld).
      - What the game writes is taken in: at those two points and inside each moved object's CCompositeObject::Update
        (after its script and own update; after its components). Per field: unchanged (x y z bit-equal): only w /
        row 3 taken; changed nearer to where we put it than to its real place: kept, the real place worked back
        (Obj_Rotate turning the wheel); nearer its real place: that is its new real place, moved again (+ teleport)
        (Obj_MoveToNode, a component copying its own matrix into the display matrix every frame).
      - Restored (fields written back - bit-exact unless the game changed them in the backdrop - plus teleport; then
        forgotten): when a condition fails (another camera, level != 0, backdrop unloaded, setting off), when the
        menu camera or view changes (then placed again for the new one), when a skater's physics is unpaused
        (checked before its components run), before the script C functions Cleanup, UnloadAllLevelGeometry and
        UnloadScene (hook in the interpreter's C function calls, cscript_exec.cpp), and at the start of every
        LoadScene. After a level change nothing moves until the backdrop is unloaded or loaded again
        (MenuBackdrop::OnSceneLoaded -> OnBackdropLoaded). An object no longer found under its id, found replaced
        by another object, or dead, is forgotten without being touched.
      - Hooks added ("Not original - the menu_backdrop feature"): CCompositeObject::Update (composite.cpp) x2,
        CCompositeObjectManager::Update (compositemgr.cpp), RenderWorld (render.cpp), CFunc_LoadScene (scene.cpp),
        the two C function calls of CScript's statement execution (cscript_exec.cpp), MenuBackdrop::OnSceneLoaded.
        MenuBackdrop now exports BackdropLoaded, PlayingMenuCamera, ViewportCamera.

      ## Tests (src/hook/selftests_feobj.cpp, all Early; offline all 169 pass, 0 MISMATCHED)
      - TestFrontEndPlacement: a stand-in menu camera (180, 40, 300) looking along -(0.30, 0.20, 0.93): its frame,
        the placement's rotation / translation for views 1 and 2 against values worked out independently (python):
        view 1 rot row 0 (-0.537644, -0.058288, -0.841155), trans (-1963.1665, -370.8876, 6119.1508), the restart
        node -> (-1954.9877, -439.7046, 6017.8390), wol_pie_1 -> (-1963.4713, -383.3855, 6120.2317); view 2 rot row 0
        (0.461354, -0.058554, 0.885282), trans (-1813.6369, -370.8924, 5716.2086), restart -> (-1830.7417, -439.7155,
        5816.3962); determinant 1; the camera lands on the view, 100 in front stays 100 in front, its axes become the
        view's; inverse; offsets; w passed through.
      - TestFrontEndObjectStates (stand-in composite objects, a stand-in lookup): the table (49, one skater);
        nothing while off / not level 0 / no backdrop / no menu camera; pose sampling (not from render steps, two
        equal in a row, a moving camera taken at its 31st sample, samples in a row only); moved (skater + 2 parts; a
        table id that isn't a composite object and an id not in the table untouched; every field, teleported);
        steady (no rewrite); restored bit-exactly and moved again for each trigger (another camera, level, backdrop,
        setting, LoadScene, Cleanup, UnloadAllLevelGeometry, UnloadScene; hold-off until backdrop gone / reloaded;
        poses kept or forgotten); other C functions nothing; view 2 / view 1; camera 03 with its own pose and back to
        02; game writes (a position written near the shop moved there and restored there exactly; a 22.5-degree turn
        about the object's own axis kept and restored as the original turned the same way; w-only changes); skater
        physics unpaused -> restored, not moved while unpaused, moved when paused again; a skater without the
        component never moved; destroyed / replaced / dead objects forgotten untouched (also when restoring).
      - TestFrontEndObjectHooks: CCompositeObject::Update (ours) on moved stand-ins with a recording component: a
        component's write near the shop moved before the update ends; a skater unpaused restored before its
        components run (they see the real position).
      - TestFrontEndObjectsGameState: Update() on stand-in game state (skate module + career level, scene list, the
        movie manager's playing animation 0x0057B030, the viewport camera 0x004A09C0, FindObject and the manager's
        GetFirstComponentByType stood in, the camera component -> camera object): each condition missing, sampling
        through render / logic updates, another camera, a level, view 2, the setting off, no skate module.
      - Mutations: 64 deliberate bugs (table, composite check, physics flag, classification, w / row 3 handling,
        each field written / restored, teleports, the placement's frame / rotation / translation / offset / inverse,
        the state machine's triggers, hold-off, pose sampling, the C function hook, the glue, the two hooks in
        CCompositeObject::Update): 55 caught at once, the other 9 after strengthening (only row 2 changed, a little
        move kept without teleport and worked back as a point, restore without a hook first, velocity as a
        direction, w kept when written again, a dead object still found, samples in a row across cameras, a turning
        camera). One unrelated failure seen once in a mutation run ("PC async handle (synchronous I/O)"); clean runs
        pass.
      - Register scan clean (only the known 0x004EFF92 PUSH ECX), call audit no mismatch, check_code_writes no
        overlaps. No game function rewritten (only feature calls added to rewritten ones).

      ## What to check in the game (menu_backdrop = 1, then 2, then 0)
      - thug2-decomp.log: "menu_backdrop: the front-end objects placed for camera FCC018D3 at (x, y, z)" once the main
        menu is up (FCC018D3 = mainmenu_camera02; 8BC72845 = mainmenu_camera03 for the options / network menus) -
        those numbers are the shop camera's real pose (tell me if a "(the camera still moving)" appears).
      - Main menu: the wheel and the skater in front of Los Angeles, where they were on screen in the skate shop
        (same size and place relative to the menu). Spin the menu (up / down): the wheel turns, the skater does his
        spin animations, nothing jumps or drifts. Lighting on the skater may differ (the shop's two level lights
        don't reach LA) and his shadow may be missing or odd (no ground under him in LA - no LA collision loaded).
      - Options / network menus (camera 03 / SS_MenuCam): whatever of the wheel / skater the original showed from
        those cameras appears, placed for that camera. In the options menu the skater may do tricks (physics on): he
        then stays in the shop (not moved), i.e. not visible - expected.
      - Create-A-Skater, skater select, CAG: the shop as before, the skater there (moved back before the first frame
        drawn with that camera); back to the main menu: in LA again.
      - Start any level, Los Angeles included, and go back: the skater starts at the level's restart as always, and
        the front end shows the wheel / skater in LA again.
      - menu_backdrop = 0: everything exactly as before (nothing moved; no "placed" log line).

      ## Risks
      - The menu camera's field of view: the held view uses the default angle; if a menu camera animation sets
        another, sizes differ slightly. Fine-tune with the table's offsets.
      - A component writing absolute values every frame in the backdrop's region, or a script's absolute
        orientation-only write (Obj_SetOrientation), would be taken as a change in place (no such writer known for
        these objects).
      - The front-end camera is captured from the camera object: if a menu camera's pose isn't steady (bone-
        targeted), it is taken after 30 samples where it is then.
      - Level-change paths that don't go through the script C functions (a net client told by the server: 0x00565A00
        calls Cleanup directly) restore at the next LoadScene instead (the wheel parts die with the cleanup; the
        skater is put back before the next level's restart).
      - Particles / sounds of the shop (ATM sparks...) are not moved.
