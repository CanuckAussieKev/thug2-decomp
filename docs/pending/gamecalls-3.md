# GameFunction calls into rewritten code -> direct calls, part 3 (agent GC3: obj, net, sys, core, nx, mc)

The user's cleanup (no new decompilation): calls under src/game/obj, net, sys, core, nx and mc that still reached
functions we've rewritten through `GameFunction<...>(0xADDRESS)` (and the files' own wrappers - `Call<R>(0x..., obj,
...)` templates, `Create(0x...)` / `GameSingleton<kCreate, kRelease>` - that are GameFunction calls through a named
address) become direct C++ calls to our functions. Worktree branch worktree-agent-aeaa04eadf21befa3. Offline only
(thug2_check on THUG2PM.exe and the stock THUG2.exe); nothing here has been run in the game yet.

How the self-tests keep covering the converted calls: every `{0xADDRESS}` stand-in list in src/hook for a converted
address got our function too (`{0xADDRESS, AddressOf(&Ours)}` - out/gc3/stand_in.py did it for all tests, not only
this batch's; the `kit.Add(...)` style lists with their own `Code(...)` argument were left alone), and the stand-ins
written another way (Patch / JumpTo / tables of pairs) were changed by hand where the converted code is tested.

Shared-file edits (small, additive): stand-in address lists in many src/hook tests (only `, AddressOf(&Ours)` added),
src/hook/viewer_testkit.h (ScriptExists' stand-in).

- [x] Batch GC3-1 (NOT run in the game yet; sys / core / mc / nx and the first net files - 96 calls):
  - sys/main.cpp: the singletons with source made and released directly (Mdl::CreateManager / ReleaseManager,
    Inp::Create/ReleaseManager, Ass::CreateAssetManager / ReleaseAssetManager, Sfx::CreateManager / ReleaseManager,
    CPlayerProfileManager / CGameRecords Create / Release, GameNet::Manager::Create / GameNet::ReleaseManager,
    Front::Create / DeleteScreenElementManager), SetKeyNames / SetGermanKeyNames, CPathManager::Instance,
    RegisterComponentTypes, Inp::Manager::SetOverridePadWithStick, the task lists (Tsk::Stack::AddTask /
    RemoveAllTasks) and the module manager's Register / Start / StopAll / Unregister. The four singletons without
    source (0x00553DB0 / 0x00552F70, 0x005691A0 / 0x00564C10) keep `Create(0x...)` / `Release(0x...)`.
  - sys/mlp.h: Mlp::Manager's three task lists are Tsk::Stack now (were 0x3C-byte arrays; only obj/compositemgr.cpp
    and obj/objman.cpp read them, by offset). sys/mlp.cpp: ServiceGameSpy, the render targets' release / restore
    (Nx::ReleaseRenderTargetTextures, ReleaseRenderTargets, RestoreRenderTargetTextures, CreateRenderTargets; the two
    at 0x004D67F0 / 0x004D68C0 have no source yet), Tsk::Stack Construct / Destruct / Process (the profiling flag
    passed as the unused argument, as the original).
  - sys/viewer.cpp: BaseTask::Remove / Destruct / Construct, Stack::AddTask (the main loop's logic list, an input
    server's handler list), Script::ScriptExists, the app's Dispatcher::Deinit, Script::ReadFromBuffer, and the
    handler for message 0x21 is &GameNet::HandleNothing (was the game's address). sys/module.cpp: BaseTask::Construct.
  - core/singleton.cpp: the SingletonPtr types for the sound, asset, profile, records, network game, screen element
    and time-of-day managers are real types calling ours (the two whose Create returns nothing - Sfx, the screen
    element manager - have `Type = void`: no SingletonPtr of them is ever constructed). core/heap.cpp:
    Mem::Manager::GetNamedHeap, the heap dump is CFuncs::CFunc_DumpHeaps(nullptr, nullptr).
  - mc/memcard_cfuncs.cpp: Pcm::PauseMusic / PauseStream, Script::ReadFromBuffer; the card's "always true" method
    (folded into 0x005B9760) is Stubs::ReturnTrue(). nx/coll_static.cpp: Nx::RegisterMovable (the original pushes a
    second argument RegisterMovable never reads; ours passes one - anim9's stand-in no longer records it).
  - net/service.cpp: SetServiceState, BaseTask::Remove, Pcm::PauseStream, SetLobbyId (called with a thread id - the
    name is established, its other callers use it the same way), the lobby's ClearLobbies / StartListingGames /
    GetLobby / FillLobbyList. net/buddy_man.cpp: GameNet::IsThug2Location (server_list.h), SetLoggedIn, the buddy /
    message nodes' DestroyBody, LobbyMan::GetPeer. net/chat.cpp (PartyMod's chat): GetLocalPlayer, the client's
    EnqueueMessageToServer, CStruct::Init / Clear / AddString, Script::DeleteStruct (its local "ScriptStruct" is
    Script::CStruct now; its own kNameSize dropped for GameNet's - the same 16).
  - net/gamenet_join.cpp: the manager's counts / limits / password / AddJoiningPlayer / SetReadyToPlay /
    ChooseStartPoints / CancelJoin, StatsMan::GetRank, the rating (the original reads it through
    GetPersonalStats + the folded getter 0x00469490, Spt::PointerMember::Get: ours reads GetPersonalStats()->m_rating),
    BuddyMan::SetStatus, GetCdKey, Tmr::GetTime, NewPlayerInfo::Construct / DestroyBody, and the match client's
    handler is &HandleServerResponse. Its local copy of the NewPlayerInfo layout is gone (GameNet::NewPlayerInfo from
    player_info.h; the object id passed on sign-extended as before) and kPlayerLocal / kPlayerObserver come from
    player_info.h.
  - Odd: gamenet_join's SetPresence(m, status, location, text) passed its arguments on as SetStatus(status, text,
    location) - the names were swapped, not the order (every caller passes the status text second, the location
    third, which is what BuddyMan::SetStatus's names say): renamed SetPresence's parameters to match.
  - Tests changed: TestMain (every converted Create / Release, the key names, task and module calls), TestMlpManager,
    TestMainLoop, TestSingletonPtrs, TestServiceTask, TestLoadFromMemoryCard, TestViewerModule (and HandleNothing
    aliased to 0x0052DAC0), TestViewer (tod2), TestModules (mva2), TestAnimCollTurnScale (anim9), the buddy tests,
    TestThugproChatHandler (its stand-ins also patched over our CStruct::Init / AddString / Clear, DeleteStruct and
    Client::EnqueueMessageToServer), TestGameNetJoin (the answers stand in for ours; HandleServerResponse mapped to
    0x005475C0); plus the `{address}` lists of every other test touching these addresses (out/gc3/stand_in.py).
  - Checks: thug2_check on both exes 0 MISMATCHED, 771 early self-tests all passed (as main); check_code_writes no
    overlaps. Mutations (out/scripts/mut1.ts): 47, 44 caught; missed: heap-dump and heap-named (TestHeap has no named
    heap 0x502D3FD2 and no failed allocation that asks for the dump - untested, as before the change), join-objid-sign
    (the object ids in the join tests are below 0x80, so the sign extension isn't exercised - untested).
- [x] Batch GC3-2 (NOT run in the game yet; the rest of net): every call into rewritten code in src/game/net is direct
  now - the handler helpers (gamenet_handler_util.h: PlayerByConnection / PlayerByObjectId / LocalPlayer /
  KingOfTheHill, PlayerInfo's IsLocalPlayer / IsObserver / GetConnHandle through AsPlayer(), the player list
  searches), gamenet_flow / handlers / game_handlers / game_cfuncs / game_commands / commands / lobby /
  lobby_cfuncs / lobby_menus / lobby_peer / manager_create / members / newplayers / players / player_lists / qr2 /
  server_handlers / session_handlers / settings / tasks, lobby.cpp, player_flags, player_info, score_list,
  server_browser, server_list, stats, stats_man, net_app, net_client, net_manager, gamenet_observer.h. The calls go
  to PlayerInfo, Manager, LobbyMan, BuddyMan, StatsMan, VaultMan, the skater (CSkater), the crown, the trick object
  manager, the park editor, the skater profile, the walk camera and the skate module's AddClientHandlers.
  - New (no addresses of their own, both declared in gamenet_manager.h): Manager::NextNewPlayerInfo - the joining
    players' next, which the original reaches through the lists' generic next folded at 0x004573D0
    (Net::App::NextConnection, with the manager as its unused this) - and Manager::UnlinkAllNodes (__fastcall, head
    in ECX) - the original's call to the list code folded at 0x00588B70 (Obj::TrickObjectHead::UnlinkAll) from
    Manager::DestroyBody. Their stand-ins: 0x004573D0 / 0x00588B70's tests cover them.
  - Folded functions whose names don't fit the caller, written as what they do (no call): the manager's last leading
    team (+0x1D4) set to -1 in HandleStartGame (the original calls PlayerInfo::SetReadyQueryOwed's 0x005430D0 on the
    manager); the goal count and the stats' rating read as their first words (the original: Spt::PointerMember::Get,
    0x00469490) - the tests now put the value in the arena instead of standing in for 0x00469490. The folded getters
    with neutral names (GameNet::FoldedGetters::Byte10 / Word04 / Word18) are called directly. The network cable
    check is Stubs::ReturnFalse() (0x005B9E50, the network object's method on the PC).
  - Kept as they are (function addresses passed as data, not calls): the GameSpy callbacks and threads
    (buddy_man.cpp kOnBuddyRequest / kLoginThread, service.cpp's news / motd threads, lobby.cpp's peer callbacks,
    ConnectThread, the tasks' codes, qr2's key callbacks, stats.cpp's OnTopStats, gamenet_lobby_cfuncs / _peer's
    "no progress" 0x005480A0) and the join's handler table (gamenet_join.cpp) - they reach our code through the hooks;
    for a game without the exe they're to become &Function too.
  - Odd: lobby.cpp's LobbyMan::Destroy calls LobbyListNode's destructor body (0x00540860) on the lobby itself (the
    header says so; kept). gamenet_commands.cpp's SetLobbyState / SetServicesState call the manager's SetServiceState
    / SetNextServiceState (names differ, behaviour as before). StatsMan::GenerateAuth is stdcall but the original
    passes the online service in ECX too (unused). gamenet_players.cpp kept its own copy of NewPlayerInfo's layout
    (now GameNet::NewPlayerInfo, player_info.h, as in gamenet_join.cpp). GetPlayerScore passes its 32-bit id to
    PlayerByObjectId, which reads 16 bits: the test records the 16.
  - Tests changed: every {address} stand-in list for the converted addresses (out/gc3/map2.txt, map2b.txt), the loops
    of plain addresses in selftests_net_lists.cpp, selftests_skater_gamenet2.cpp and _gamenet5.cpp made pairs,
    TestRetrieveTopStats patches our VaultMan::Download too, the 0x005B9E50 stub in net_newplayers is cdecl (no ECX),
    GenerateAuth's stub doesn't record ECX, GameNetKit::Str qualified where skater.h's namespace Str now clashes.
  - Checks: thug2_check on both exes 0 MISMATCHED, 771 early self-tests all passed; check_code_writes no overlaps. Mutations (out/scripts/mut2.ts): 40, all caught.
