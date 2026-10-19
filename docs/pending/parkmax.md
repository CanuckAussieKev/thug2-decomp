# Park editor player-count commands (agent "parkmax")

- [x] Batch PM-1 (NOT run in the game yet; the park editor's player-count script commands - the stock
  no-CD exe's damaged parameter names): src/game/ed/park_cfuncs.cpp (new); park.h (CParkGenerator::GetMaxPlayers
  reads the whole int at +0x20 - it had no callers and read a byte -, GetMaxPlayersPossible 0x00591D30 declared;
  the two CFunc declarations); selftest_runner.cpp (kReferenceBytes: the names' run).
  - 0x0058E120 CFunc_GetParkEditorMaxPlayers (`bool __cdecl (CStruct* params, CScript* script)`, params unused):
    the park manager (0x00595200 CParkManager::sInstance) -> its generator (+0) -> the int at +0x20, added to the
    script's parameters (script+0x14) by CStruct::AddInteger(const char*, int) 0x00477C30 as "MaxPlayers"
    (0xB7E39B53). Returns true.
  - 0x0058E140 CFunc_GetParkEditorMaxPlayersPossible: the same with the generator's 0x00591D30 (thiscall, no
    arguments, returns int: the players the park has room for, from the park memory's use 0x0059E730 and
    MaxPlayers; stays GameFunction) as "MaxPlayersPossible" (0x1FD6359C). Returns true.
  - **Why (not a no-op, not optional)**: the user's main install runs a stock, unwrapped no-CD THUG2.exe
    (C:\Games\THUG2\Game\THUG2.exe) unmodified. It differs from the reference THUG2PM.exe in 172 bytes
    (tools/game/exe_audit.py); one run, 0x0064F29C-0x0064F2B9, is these two commands' parameter names: THUG2PM.exe
    has "MaxPlayers\0\0MaxPlayersPossible", the stock exe 30 bytes of 0x90 filler (damage from the no-CD build's
    maker). The originals pass pointers to those strings, so on the stock exe both commands added their value
    under the checksum of the filler and the scripts' <MaxPlayers> / <MaxPlayersPossible> were never set (net.qb's
    created-park player counts, the level select's and memory card's "too many players for this park" checks).
    Ours write the names as string literals (the original call takes a name string; AddInteger(const char*)
    checksums it), so both commands behave as the original on an undamaged exe whichever exe runs. Said in a
    comment at the top of park_cfuncs.cpp.
  - kReferenceBytes (src/hook/selftest_runner.cpp): the 0x0064F29C run added (stock 0x90 x30 -> THUG2PM.exe's
    names), so on the stock exe the early tests compare ours with the undamaged original.
  - Tests (selftests_parkmax.cpp): TestParkMaxPlayers - a stand-in park manager (0x007CE604), two generators, two
    scripts and four parameter structures; six player counts (1, 8, 0, -3, 0x01020304, 2) through both commands,
    original vs ours, with AddInteger(const char*) (0x00477C30 and ours) and 0x00591D30 recording stand-ins: the
    structure, the name (text and checksum), the value, the generator asked, the return values and the stand-in
    block compared; and the original's names checked to be MaxPlayers / MaxPlayersPossible.
    TestParkMaxPlayersDamagedNames - 0x90 written over 0x0064F29C-0x0064F2B9 for the test (the stock exe's
    damage): ours must still add MaxPlayers / MaxPlayersPossible by checksum, and the original must not (the
    damage reached what it reads).
  - Mutations: 18 deliberate bugs, all caught at first (each name misspelt; each name read from the exe's address instead
    of a literal - caught only by TestParkMaxPlayersDamagedNames; the two values swapped / off by one; the
    command's params argument instead of the script's; returning false; the manager taken for the generator;
    +0x20 read as a byte / at +0x24; each AddInteger left out). Also by hand: the kReferenceBytes entry moved
    one byte (so it never applies) fails TestParkMaxPlayers on the stock exe (the original adds the filler's
    checksum).
  - Checks: thug2_check on THUG2PM.exe (C:\Games\THUG2_PM_BAK) and on the stock C:\Games\THUG2\Game\THUG2.exe:
    0 MISMATCHED argument sizes, all 302 early self-tests pass on both (the stock exe: "THUG2PM.exe's
    bytes in its 11 differing spots"). scan_register_assumptions: nothing new; audit_game_function_calls: no MISMATCH;
    check_code_writes: no overlaps.
  - Left: SetParkEditorMaxPlayers 0x0058E0F0 next to them (reads "num_players" by checksum - undamaged) has no
    source yet.
  - Switch off: `[replacements] disable=park_cfuncs.cpp`.
