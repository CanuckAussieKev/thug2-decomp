- [x] PartyMod round, misc (NOT run in the game yet; early tests pass offline; branch pm/misc): three functions
  PartyMod patches, with its changes built in (the original's logic kept in comments).
  - 0x004E1EF0 `Sys::Main(argc, argv)` (src/game/sys/main.cpp; cdecl, returns 0): Config::Init 0x5B60B0, standard
    heaps, the language start-up (German 0x4E0FE0, else 0x4E0DA0), the argument dump through Dbg::Printf, "demo" as
    the only argument sets 0x7CC978, SetThreadExecutionState(continuous | system | display), InitialRand(0x6680BF3),
    sin table; then while the quit flag (0x7D6A2C) is clear: bottom-up heap pushed (virtual slot 0), symbol table,
    hash item pool (0, 10000), Pre manager, timer, async file system, the 23 singletons (system then game; their
    holders' vtables 0x64AFB0-0x64B000 on the stack are only for exception unwinding), three tasks into the main
    loop manager's lists (+0x7C: Mdl's and Sio's tasks; +0x04: 0x44E7F0's), six modules registered and started
    (in two different orders), the engine start 0x48C1D0(350), MainLoop. **PartyMod: fastExit() after MainLoop**
    (in place of the original's PopMemProfile call at 0x4E2492) - the process ends there. The teardown after it
    (stop all modules, five unregistered - 0x476040 never is -, the three task lists emptied, releases in reverse,
    Pre::DestroyManager, heap popped) is kept as code: reached only if fastExit returns (in the test). The memory
    profile push/pops (empty 0x5480A0 calls) are kept with their names.
  - 0x00545880 `Net::ServiceTask(task)` (src/game/net/service.cpp; cdecl task callback, the network manager is
    the task's +0x1C): 18-state machine at manager +0x1B8 - wake-up timer (state 0), GameSpy availability check
    (3-5; "mototrax"), news (6/A start a suspended download thread 0x545540 - its id via 0x533240, ResumeThread;
    7 waits; B ends), message of the day (8 thread 0x545620, C waits 15 s, 9 shows it), lobby list (D-11).
    **PartyMod's MOTD fix**: news timeout 500 ms (original 15000, 0x545B4F), script 0x588ED87C (original
    0xD4E84C8D, 0x545CDE) and +0xFE09 = 1 (original 0, 0x545CE8). Note 0x6450E0 is ResumeThread (not
    CloseHandle): the thread handles are never closed (as in the original).
  - 0x005A8730 `Mc::LoadFromMemoryCard(params, script)` (src/game/mc/memcard_cfuncs.cpp; CFunc): Name / Type /
    integer 0x8C6808D4 ("no apply"), path per platform (0x7CEFA8: 1-3 console folders, 5, 4/7 the card's
    ConvertFilename of the type's pattern), open, size must equal the type's (0x5A7340), read, header {crc, ?, ?,
    size, version}: size <= 500000 and the type's version (0x5A6210); two structures read (0x47A0C0), the second's
    bytes minus 0x69 for type 0xCA41692D; applied by 0x5A81B0 (EAX type, ESI contents, stack script, params) or
    kept in 0x7CECD4/0x7CECD8 (type 0x61A1BC57 also applied with params null); RunScript 0x0EB9402D when applied.
    Failure checksum 0xFBF9C22A into the script's params. Music/streams paused unless platform 4. **PartyMod:
    the save CRC isn't checked** (its NOP of the JNZ at 0x5A8A01; the CRC test kept in a comment). The type
    helpers take register arguments (EAX/ECX/ESI/EDI): called through inline asm.
  - Tests (src/hook/selftests_misc.cpp, Early): a generic recording stand-in (`StandIns`: PUSHAD thunk calling a
    std::function handler, RET n) written over every callee - the game's address and our own version alike, and
    kernel32's CreateThread / ResumeThread / SetThreadExecutionState (plus the game's import slots) - with
    PartyMod's patch bytes written into the original. TestServiceTask: every state incl. out of range, wake
    times around now, all GameSpy answers, message counts 0/3/-2, both timeouts on both sides of their limits
    (and wrapped), finished flag, lobby empty or not; manager memory compared. TestLoadFromMemoryCard: 10
    platforms, no card / converted name / file, missing name/type, wrong size, short read, header size
    500000/500001/negative, wrong version, the obfuscated type (with and without bytes to decode), kept or
    applied, null structures. TestMain: 7 command lines (demo, demox, dem, extra args, argc 0), languages, quit
    already set, 1 and 2 loops; every call with its arguments, the demo flag, 0x6FA7D0. 50 deliberate bugs (20 service, 15 memory card, 15 main), all caught by the test meant to catch
    them (s_motd's first run showed a one-off failure of TestAsyncHandlePc instead; rerun alone, caught by
    TestServiceTask; five clean baseline runs after).
  - tools/check_partymod_overlap.py HANDLED: 0x4E2492, 0x545B4F, 0x545CDE, 0x545CE8, 0x5A8A01.
  - src/partymod unchanged: its runtime patches stay (bypassed by our functions; still in effect if a
    replacement is disabled in thug2-decomp.ini).
  - Register scan (only the known 0x4EFF92 false alarm) and call audit (126 GameFunction uses, no mismatch)
    clean; PartyMod overlap check clean.
  - Not done (find_gaps now lists them between 0x4E1950 and 0x4E1EF0): the ~50 small functions at
    0x4E1970-0x4E1EE0 - the singleton holders' methods (11-byte `this->ptr = arg; return this` constructors,
    33-byte destructors / vtable entries of the holder vtables 0x64AFB0-0x64B000) that main's stack holders use
    only for exception unwinding. They belong with a SingletonPtr<T> class; next to pick up.
