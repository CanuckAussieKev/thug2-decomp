# GameFunction cleanup, part 4 (agent GC4: src/game/front, src/game/gfx, src/game/pcm)

Calls from src/game/front, gfx and pcm into functions we have already rewritten, made direct C++ calls instead of
`GameFunction<...>(0xADDRESS)` (as batch 342 did for the park editor). No new decompilation. Tests are offline
(thug2_check on THUG2PM.exe and the stock THUG2.exe); nothing here has been run in the game yet.

Shared-file edits (small): src/game/pcm/stream.h (the platform's SetMusicVolume renamed PlatformSetMusicVolume - it
clashed with music.h's front-end SetMusicVolume, same parameters, different return type, so no file could include
both; OpenUserTrack declared), src/hook tests listed per batch.

- [x] Batch GC4-1 (NOT run in the game yet; GameFunction cleanup - src/game/pcm, 61 calls): every call from
  src/game/pcm into rewritten code is direct now - music.cpp (21: the platform's stream / music status, pause,
  volume, update, stop, the user soundtrack queries, PlayTrack, PlayTrackByName, ApplyTrackVolume, StopMusic,
  UpdateStreamVolume, Sfx::GetMainVolume), music_api.cpp (22: the platform layer, StreamExists, LoadMusicTrackByName,
  PlayUserTrack, PlayMusicTrack, the preload checks / starts, InitPlatform; UpdateStreamVolume's sound volume is a
  real Sfx::sVolume now - Init / SetSilent / PercentageAdjustment, CSfxManager::GetVolume, CStreamComponent's
  GetStreamExtent / GetStreamPosition - in place of a local copy of the struct), stream_commands.cpp (13: the same
  platform calls, FamData::Load, sVolume's methods and CSfxManager::IsPaused / GetVolume - the local `Volume` struct is
  Sfx::sVolume), stream_play.cpp (4), stream.cpp (2: BinkStreamView::Construct / Open over the BinkStream),
  bink_stream_files.cpp (NewOverlapped). Left in pcm: 0x004A09E0 (the nearest emitter, not rewritten).
  Behaviour notes: the original passes PlatformStopMusic (0x00453AA0) 1 or 0 and StartPreloadedStream (0x00454270)
  the pitch - neither takes them (our versions have no such parameter); LoadStream's two unused arguments still get
  the pitch's bits and the `stopped` flag. Names that don't say what the caller does: the platform's PlayStream
  (0x00454200) is what the front end's StartStream uses to preload (it loads without starting - the helper keeps the
  name PreloadOnChannel); CSfxManager::IsPaused is what PlayStreamFromObject calls "no sound please" (kept as a
  helper of that name). 0x0047C790 (Sfx::GroupCount, a linker-folded `return -1`) was stream_play's "open the user
  soundtrack's track": not called as GroupCount - a new Pcm::OpenUserTrack returns INVALID_HANDLE_VALUE (declared in
  stream.h so the platform test still stands in for it). Sfx::GetMainVolume and CreateManager were called through a
  "manager" pointer the original passes in ECX (GetMainVolume reads a global): our calls take none. Overloads now
  visible together (music_api.h / stream.h / stream_commands.h: StreamExists, StopStreams, StopStream,
  SetStreamVolume, PlayStream) made five REPLACE lines and four test Picks name the overload with a static_cast.
  Tests: the stand-ins for every converted address in selftests_mva3 (music API), mvb (playlists, stream commands,
  Bink stream files), gapsA (music streams, track start, per-frame update), pcmplay (stream platform / play: the
  Bink stream view, OpenUserTrack, the sound manager and its main volume - FakeSoundVolume written over ours too) and
  selftests_pc's TestPlayNextTrack (JumpBoth to MusicStatus / ApplyTrackVolume / PlayTrack) cover our functions too.
  Two mva3 stand-ins record fewer arguments (0x00453AA0: none, 0x00454270: two) - the original's extra ones are
  unused and our direct calls don't pass them. Mutations (out/scripts/mut_gc4_1.ts): 16 / 16 caught - a stream's
  channel status, the update's StopStreams dropped, playlist 1's track looping, a soundtrack name's index, the sfx
  volume, StreamExists' music flag, the user track, the percentage adjustment dropped, InitPlatform dropped, the
  stream extent dropped, LoadStream's plays, the fam checksum, IsPaused inverted, the platform's sound volume, the Bink
  open's offset, NewOverlapped's result.
- [x] Batch GC4-2 (NOT run in the game yet; GameFunction cleanup - src/game/gfx, 80 calls): every call from src/game/gfx
  into rewritten code is direct now - the renderer's state (render_state.h: SetState, SetTexture, SetBlendMode,
  SetVertexFormat, SetPixelShader, SetWorldTransform) from render.cpp, flash.cpp, movie.cpp, text.cpp; render.cpp's
  Draw2D, Bloom, the camera's GetAdjustedHFOV / GetPos / GetMatrix (GetPos / GetMatrix are source-only 4-byte
  getters: now inline reads of m_pos / m_matrix, +0x5C / +0x6C as the originals), RenderScene, BuildOccluders,
  Sun::Render, UpdateGlows, Tmr::GetTime, RenderShadowTextures, BeginScreenEffects, CopyScreenQuarter;
  render_state.cpp's CreatePixelShaders; text.cpp's Sio::GetButtonKeyName and SDraw2D's Construct / DestroyBody;
  movie.cpp's sound manager, main / music volumes, StopMusic, StopStreams; engine_glue.cpp (the occlusion polygons,
  below); lens_flare.cpp (GameClockTime / TimeSince, Mth::Vector::MultiplyBy); anim_controller.cpp
  (CAnimBlenderComponent::GetAnimSet / GetValue, GameClockElapsed); anim_blend.cpp (Walk::Lerp); device.cpp
  (Tmr::SetFlags; the debug font through Stubs::ReturnZero, the shared stub the original calls); tod_manager.cpp
  (Sun::SetSun - the local SunParams reinterpreted as Sun::Settings, same offsets; CModelComponent::SetColor);
  font.cpp. movie.cpp's anonymous D3D enum kCullMode / kLighting renamed kD3DCullMode / kD3DLighting (they clashed
  with render_state.h's RenderStateId).
  Not called by their REPLACE names (linker-folded): font.cpp's SetSpacings went through 0x004E3F90 (REPLACEd as
  Obj::RefreshFromStructure, a naked tail jump to vtable slot 2): it calls the font's slot 2 itself now;
  lens_flare.cpp's Prim::SetBlendMode / SetUnknown14 went through 0x00428270 (CLineObject::SetBlendMode) and
  0x00407A50 (CAssMan::SetPermanentByDefault): now the field writes those do (the dword at +0x0C, the byte at +0x14).
  Odd: engine_glue.h's names don't say what the calls reach - AddDebugShape(4, ...) is occlusion.h's
  AddOcclusionPoly (its "colour" is the polygon's checksum: coll_tri.cpp passes a checksum), FlagDebugLines is
  EnableOcclusionPoly, ClearDebugLines RemoveAllOcclusionPolys, and SphereVisible is SphereOutsideView (1 when
  OUTSIDE the view, not visible). Names kept for their callers; the header's comment says so now. SetTexture's
  third argument (0, or the font's unknown17C) and the "vertex shader" (0, 0) calls: ours takes (stage, texture)
  and SetPixelShader(shader, material) - the 0x004DAA80 callers were calling the pixel shader set with nulls.
  Tests: stand-ins now over ours too in selftests_gfx2 (blur pass, world render: a patch over each of ours),
  selftests_pc (TestDrawFlash, TestDrawMovieFrame, TestPlayMovieFile: patches / JumpBoth over ours), gapsA
  (TestSTextDrawing), selftests_text (TestQueryString, TestDrawText: GetButtonKeyName; TestInitDirect3D:
  Stubs::ReturnZero - the shared stub, only for that test - and Tmr::SetFlags), fe_sprite (engine glue),
  obj_lens_flare, tod (SetSun, SetColor), engcmd1 (SetSun), fe_anim, appmain (CreatePixelShaders). Stand-ins that
  recorded SetTexture's third argument (BlurTexture, StandInState3, MovieSetTexture, PmRendererTexture, gapsA's) and
  PmSoundVolume's ECX no longer record them (ours doesn't pass them; unused by the callee).
  Mutations (out/scripts/mut_gc4_2.ts): 19 / 20 caught - RenderScene's pass, UpdateGlows' flag, BuildOccluders'
  frame, the camera angle, the flash's blend mode, the movie's address mode and music volume, the text's texture
  stage, SDraw2D's hidden flag, the key name's button, EnableOcclusionPoly's flag, the lens flare's TimeSince and
  its +0x14 byte, the partial anim's length, the model colour, SetFlags dropped, the font spacings swapped, Lerp's
  t, CreatePixelShaders dropped. Not caught: Draw2D dropped in Draw2DWithBlackBars - the world render test runs
  ours in both runs there (PartyMod's call at 0x004B341D into ours): untested.
- [x] Batch GC4-3 (NOT run in the game yet; GameFunction cleanup - src/game/front, 69 calls): every call from
  src/game/front into rewritten code is direct now - the level editor's 13 collision rays (CFeeler::Construct /
  GetCollisionAlong over the same 0x100-byte buffer, read back at +0x34 as before; GetCollisionAlong's int result
  tested for non-zero where the GameFunction read AL - it returns 0 / 1), its render states (SetTexture /
  SetVertexFormat / SetPixelShader), Terrain::GetTerrainIndex, Matrix::InitFromAngles, Quat::FromMatrix / GetMatrix
  (the chains' quaternions are Mth::Vectors: reinterpreted), SkateScript::GetNode / GetPosition, the camera's getters,
  Script::GetFloat (the original passes 1 as a second argument our GetFloat doesn't take), GameNet::Manager::InNetGame
  (automation, layout); front_end.cpp (Mdl::Module's Construct / DestroyBody, Tsk::BaseTask's Construct / Remove,
  Tsk::Stack::AddTask, Inp::CreateManager / ReleaseManager / SetOverridePadWithStick, Sfx::UpdateVoiceStates - the
  original calls it as a method of the sound manager, it takes none - and CSfxManager::UpdatePositionalSounds,
  Tmr::PauseGameClock, CScreenElementManager::SetPausedAll / Update); menu_element / screen_element / mouse_menus
  (the screen element manager's HasPriorities, IsPadEvent, IsTopPriority, GetElement, ResolveId, SetParent,
  DestroyElement); screen_element_cfuncs (the text / text block methods, and both constructors - GameConstruct gone);
  screen_element_manager (Inp::Manager::GetPlayerController, CTextElement::SetText); element3d; menu_backdrop
  (CMovieManager::GetPlayingName); text_block / text_element (the blur's destructor: Stubs::DoNothing, the shared
  empty stub the original calls). The named-address helpers (sprite_element's / text_element's Set(address, ...),
  level_editor_goals' GoalCommand) reach no rewritten function: left.
  Tests: stand-ins over ours too in selftests_menu (front end: the thiscall helpers take ours; 0x0047B000's no longer
  records ECX), fe_element_fixture.h, fe_menu, fe_manager, fe_cfuncs (the text helpers and the two constructors),
  fe_text (0x005480A0 over Stubs::DoNothing too, ECX not recorded), el3d, menulvl, net_scores (GetElement: the score
  list reaches it through Front::GetElement now). Mutations (out/scripts/mut_gc4_3.ts): 17 / 17 caught - a task's
  priority, the stick override, the clock pause, the voices update dropped, the input manager's release dropped, the
  top priority id, DestroyElement's recurse, ResolveId's name, a text block's last-line flag, the text length, the
  block string's size, GetNode's assert, element3d's camera angle, the player's controller, the score text, the blur
  destructor dropped, the backdrop's playing name. Untested: the level editor's rays (snap-movables: no test reaches
  them - the editor is UI, tested by hand); one mutation (text-ctor) didn't build. Odd: with the test filter
  "Menu,FrontEnd,ScreenElement,Text,Element3d,NetScores,Backdrop,Sprite,LevelEditor,Editor" TestEditorUnproject
  hangs (45 s watchdog) even without a mutation - it passes alone and in full runs (D3DX's first-call dispatch after
  other tests?); not from this work, worth a look.
