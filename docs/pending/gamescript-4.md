# Game script commands 0x005B0A40-0x005B6560 (tricks, input, face textures, theme colours, skater init) - gamescript-4 agent

Branch worktree-agent-a6504c6a3ac016365. Tests in src/hook/selftests_gs4_*.cpp (shared pieces in src/hook/gs4_testkit.h).
Shared files touched: CMakeLists.txt (new files listed).

- [x] Batch GS4-1 (NOT run in the game yet; configurable tricks, records, soundtracks, controllers - 24 CFunctions):
  src/game/script/trick_cfuncs.h / .cpp (new).
  - 0x005B0A40 FlushDeadObjects, 0x005B0A50 BindTrickToKeyCombo (the named template's or the current profile's special
    trick slot - trick, keys, cat flag, update_mappings default 1 - or its trick_mapping key combo, an integer for a CAT
    number; false for `special` without index), 0x005B0C00 GetKeyComboBoundToTrick (the mapping's key combo checked
    to still hold the trick; `special`: the slots, then current_index too and a debug line; cat_num through
    0x005A9E30 - the structure in EBX, called through an asm wrapper), 0x005B0DA0 UpdateTrickMappings (online: the
    local skater and profile 0, not checked for null), 0x005B0E30 GetConfigurableTricksFromType (type = trickType, else
    scr, else GrindTrick; `special` without a type matches any; IsSpecial must agree with `special`; grab / flip
    tricks also list the local skater's full created-trick slots 1-10 from its +0x180 pointers; false with no trick
    even when there are CATs), 0x005B1140 TrickIsLocked (flag 0x7ABFD5B4 - name unknown -: locked unless the current
    profile is the trick's Skater; otherwise the template's is_hidden), 0x005B1250 GetTrickDisplayText (ExtraTricks
    as a name or an array's first; 0xD0BCFC62 - name unknown - its name), 0x005B1380 GetSpecialTrickInfo,
    0x005B1420 GetTrickType, 0x005B14A0 GetIndexOfItemContaining, 0x005B1590 GetLevelRecords (default: the career's
    level), 0x005B15F0 ResetComboRecords (0x00561400 on the local skater's score), 0x005B1610
    GetNumberOfTrickOccurrences (no null check of the local skater), 0x005B1670 GetNumSoundtracks, 0x005B1690
    GetSoundtrackName, 0x005B16D0 BindControllerToSkater, 0x005B1730 BindFrontEndToController (the pad that had the
    controller takes the other pad's old one), 0x005B17C0 ControllerBoundToDifferentSkater (local skaters 0-7 other
    than n - reads the input manager's 2-entry controller table past its end for skaters 2-7, as the original),
    0x005B1840 ControllerBoundToSkater, 0x005B1890 GetKeyComboArrayFromTrickArray (AddArray copies the array it
    made, which is never freed: a leak kept from the original), 0x005B1A00 FirstInputReceived, 0x005B1A10
    VibrateController, 0x005B1A90 LockCurrentSkaterProfileIndex, 0x005B1AD0 SetSpecialTrickInfo.
  - Test TestTrickCFuncs: original vs ours with the profile manager, profiles, skate module (0x00564F80, 0x00566520,
    0x005665C0), skaters, trick component, records, career, Pcm, front end, Sio device, the key-combo look-ups
    0x005A9D90 / 0x005A9E30, global structures / arrays and the array pool stood in for by recording code; every
    command over its branches. 48 of 48 deliberate bugs caught (1 after strengthening: update_mappings' default).
- [x] Batch GS4-2 (NOT run in the game yet; interpolating parameters, a texture's name, power-ups, the hunt, entering
  vehicles, the lobby keyboard - 11 CFunctions): src/game/script/game_util_cfuncs.h / .cpp, src/game/net/hunt_cfuncs.h
  / .cpp (new).
  - 0x005B1B60 InterpolateParameters (a new structure 0xFF6F3872 - name unknown - of A's integers / floats moved
    towards B's by 0x404E690D - name unknown -: unnamed and ignored components skipped, B's integer or float, an
    integer result truncated as _ftol does (the low 32 bits of the 64-bit value) only if both were integers; Angles
    the shorter way round through 0x005A9E80, its x87 result read at full precision; the original asks the command's
    params for the next component - the same thing), 0x005B1E00 GetTextureFromPath (the last part after / or \ into
    a 32-byte buffer, as the original), 0x005B1EA0 ClearPowerups (once per existing skater 0-7, always the object
    manager's first SkaterState component - kept), 0x005B1EE0 PlaceObject (the script's skater's PlayerInfo: the
    count at +0x34, the positions from +0x180), 0x005B1F70 AnnounceHuntStarted (message 0x93 to every remote player),
    0x005B2030 AnnounceNeedToPlaceObjects (0x91 to every player), 0x005B20E0 AtLeastOneObjectPlaced (observers not
    counted; a count > 0), 0x005B2170 ReportObjectPickup (message 0x90 with the id; offline the client numbered by
    colObjId - as the original), 0x005B2210 PlacedLessThan, 0x005B2260 BroadcastEnterVehicle (message 0x89: the
    skater's id byte and the control type, 8 bytes with padding), 0x005B2300 LobbyCheckKeyboard (a typed space opens
    lobby_enter_kb_chat unless keyboard_anchor exists).
  - Test TestHuntCFuncs: original vs ours with the network manager's players, apps and clients, the skate module,
    the object manager, the keyboard, ObjectExists, RunScript and the structure pool stood in for (messages compared
    field by field with their data); interpolation over integer / float / mixed / ignored / unnamed / angle
    components at five fractions (incl. an _ftol overflow). 37 of 37 deliberate bugs caught (2 after strengthening:
    an unnamed component in B, a negative placed count).
  - Build note: the verify build embeds its debug info (CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded, this worktree's
    build folder only): the shared compile PDB failed with LNK1318 under mutation builds.
