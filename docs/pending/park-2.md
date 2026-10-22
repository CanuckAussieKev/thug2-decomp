# Park editor & graffiti, second quarter (0x0058CD40-0x0058FBD0) - PE2 agent

Branch worktree-agent-aee5cf12d9ed9c993, workspace C:\Games\THUG2-2. Source in src/game/ed/gap_manager.cpp (the gap
manager's members in the range), src/game/ed/park_editor.h / park_editor.cpp (the park editor module, Ed::CParkEditor)
and src/game/ed/park_editor_cfuncs.cpp (its script commands); tests in src/hook/selftests_park_editor.cpp. Shared files
touched (additive): CMakeLists.txt (the new files), src/game/ed/park.h (CGapManager: its layout and the record type
CGapManager::Gap, declarations in place of the GameFunction wrappers of 0x0058CDA0 / 0x0058CDF0 / 0x0058CE90 /
0x0058CF20 / 0x0058CF30 / 0x0058D070; a forward declaration of CParkManager).
Switch off: `[replacements] disable=gap_manager.cpp` (and park_editor.cpp, park_editor_cfuncs.cpp).

- [x] Batch PE2-1 (NOT run in the game yet; the park editor's gap manager, 9 functions): src/game/ed/gap_manager.cpp
  (new), CGapManager in src/game/ed/park.h.
  - The layout (0xA8 bytes): +0x00 the next GapID, +0x04 32 gap records, +0x84 32 flags "the gap's first half
    (StartGap) was placed first", +0xA4 the park manager (null in a copy). A gap record (0xEC bytes, Calloc'd by
    StartGap / EndGap, freed by RemoveGap): its two ends' GapIDs, trigger pieces and metapieces, its name (+0x18,
    0x80 bytes) and its GapInfo (+0x98).
  - 0x0058CD40 UpdateChecklist (the current level's gap checklist - the career's, through the skate module's +0x20 -
    cleared and every gap added with 0 landed, from the GapInfo's text and score), 0x0058CDA0 SetGapInfo
    (sprintf "%s" of the info's text into the record's name, the info copied, the checklist made again), 0x0058CDF0
    RemoveGap(index, forget) (the two trigger pieces deleted through the park manager's generator; with `forget`, the
    goal editor (0x004E8470) drops the gap's checklist index from the park's gap goals (0x004E8420); every slot
    holding the record cleared, the record freed), 0x0058CE90 RemoveAllGaps, 0x0058CEC0 SetTriggerLength(piece)
    (the first gap with the piece as an end: its length on that half (0x0058CBF0) / 120 to the piece (0x005906B0);
    0 if none), 0x0058CF20 Destroy (RemoveAllGaps, the singleton cleared), 0x0058CF30 CopyFrom (the next id, each
    record copied without its pieces and metapieces, the flags; the copy's park manager null), 0x0058D070 StartGap,
    0x0058D120 FireGap(index, script) (for both ends, the one placed first first: a new structure with GapID, score,
    text - the record's name - and, for the first, combined_flags = the gap's cancel flags or the global integer
    CANCEL_GROUND; StartGap / EndGap run on the script's object through its vtable slot 3).
  - Test (selftests_park_editor.cpp TestParkGapManager): every function, original vs ours on a stand-in world
    (static, same address in both runs) with the career / checklist / goal editor / generator / memory / structure /
    object calls stood in for by recording code: checklist null / two / three gaps, RemoveGap forgetting found / not
    found / a null piece / a record in two slots, the trigger length for each end / none / a piece in two gaps, a copy
    over stale slots, FireGap both orders. 33 deliberate bugs: 30 caught, 2 more after adding the two-gaps piece and
    the singletons to the compared state; 1 equivalent (the inline constructor's m_cancelFlags = 0 after a Calloc).
