- [x] Batch (qb agent; NOT run in the game yet; early tests pass offline): the pre-compiled .qb format and
      unloading a .qb (src/game/script/precompiled.*, load.cpp). 9 functions.
      - ParsePrecompiledQB 0x46FBA0 (filename, data, unused): the image relocated in place against its own start
        and its symbols defined, through RelocatePrecompiledSymbols 0x46CA50 (file, base, callback, argument,
        flag: symbols from +0x1C until exactly the file's size at +4 - no other end check; each relocated, then
        passed to the callback) with DefinePrecompiledSymbol 0x478DE0 (a function Ghidra never made - only its
        address is pushed: sets the symbol's file checksum +8, CleanUpAndRemoveSymbol on the symbol of the same
        name if any, links the file's own entry into the table; the flag - the third argument != 0, pushed as
        all of EAX with only AL set - isn't read).
      - Relocation, each returning the end of the item's data: RelocateSymbol 0x46C970 (entry +1 byte = type),
        RelocateStruct 0x46C7F0 (the last component's end, or the structure's own end when empty),
        RelocateComponent 0x46C6B0 (type = byte 0 >> 1; next relocated when set; a script component's size is
        SkipToEndOfScript's), RelocateArray 0x46C830 (more than one element: an element table follows the
        array, relocated only when non-null; strings padded once after the last), RelocateArrayElements 0x46C7B0
        (a thiscall CArray method, RET 4: written as a free function with an adapter, struct.h unchanged).
        Integers, floats, names, C functions, member functions, 0x0B and unknown types are left alone; arrays
        of scripts (7), C functions, member functions and 0x0B have only their table relocated.
        Quirk kept: a script symbol's stored size is read from right after its entry (+0x14+8), not through
        its value.
      - UnloadQB 0x46EEC0 (file checksum; 0 does nothing): lists the symbols whose +8 is that file (array on the
        top-down heap), CleanUpAndRemoveSymbol on each in table order, then every script for which CScript
        0x472C90 (all the scripts it's running / returning to still defined?) is false gets 0x473A80 (clear)
        and 0x473010 (tells its object it stopped) - those three still the game's, through GameFunction -
        and finally Pip::Unload(checksum) (pre-compiled files stay loaded until then). Callers: 0x5A59B0
        (UnloadQB-like C function, then 0x479070), 0x5A59D0 (reload: unload then LoadQB), 0x466C20.
      - LoadQB / LoadQBData now call our ParsePrecompiledQB directly.
      Early tests (src/hook/selftests_qb.cpp): TestPrecompiledQB - a generated image with every symbol type
      (strings of lengths 0/3/5, a script whose value is another block, unknown types 0/0x14), structures with
      every component kind (three script components, with and without a line number after '$', nested and
      empty structures, arrays as components), arrays of every element type with 0, 1 and more elements and a
      null table, names that replace an earlier record of the file and symbols already in a stand-in table (and
      share buckets): each relocation function on every item (fresh copy each time; return values and the whole
      image), the walk with a recording callback / none / an empty file, ParsePrecompiledQB (both flag values),
      the callback alone, and LoadQBData's pre-compiled branch - original vs ours. TestUnloadQB - symbols of
      several files (kept and not, five types, several buckets) in a stand-in heap, four fake scripts with the
      CScript methods and Pip::Unload stood in for (recording), unloading 0, a file, a file with no symbols,
      another file - original vs ours, calls and the whole arena. 41 of 45 deliberate bugs caught (1 after
      strengthening: a replaced table symbol that isn't kept, FreeSymbol stood in for); equivalent: `size == 1`
      -> `>= 1` in the else of `> 1`, the ignored flag (twice: ParsePrecompiledQB, LoadQBData's `unused`),
      linking only when no symbol of the name is left (none ever is after the removal).
      Findings (the script runtime's spec):
      - A pre-compiled image is symbols ready to use: the file's own CSymbolTableEntry records are linked into
        the table and point into the file, which stays loaded (on the script heap) until UnloadQB. Records must
        have bit 2 (4) of byte 0 set or CleanUpAndRemoveSymbol, when a later definition replaces them, frees
        them into the pools as if they were pool memory. Nothing marks the symbols new (byte 0 bit 0 comes from
        the file), so whether RestartChangedScripts restarts scripts that use them depends on the file.
      - Defining a symbol from a pre-compiled file never calls RemoveFromScriptCache (CreateScriptSymbol does):
        a cached decompressed copy of a replaced script would stay - worth checking against the script cache
        agent's findings.
      - The shipped data\**\*.qb.xbx files (95, magic 0xB075EF6F; "xbx" is the PC's pre-compiled extension from
        0x5B6040, platform 4) do NOT fit this exe's layout: each entry starts 00 04 <type> 00 (the type in byte 2,
        e.g. 0x0C for an array, 0x07 for scripts) and components 00 <t> 00 00 with values that don't match
        this build's enum. Read as this code reads them, every entry would be a local string. So either nothing
        loads them on PC (LoadQB's callers pass "....qb" names; the LoadQB C function takes the name from the
        script) or they're loaded differently - check in the game (e.g. log LoadQB's pre-compiled branch)
        before relying on the format. The +0x08..+0x1B header bytes (1C 08 02 04 10 04 08 0C ...) aren't read.
      Next to it, not done: 0x46C470-0x46C6A0 (13 small functions of a class that wraps a CStruct at +4 -
      0x46C610 GetX-with-default, 0x46C650 add, 0x46C680 destructor... - not part of this format); CScript
      0x472C90 / 0x473A80 / 0x473010 (with CScript). find_gaps reports one thunk, 0x48C1A0 -> SetLetterbox
      0x4B2440 - not from this batch.
