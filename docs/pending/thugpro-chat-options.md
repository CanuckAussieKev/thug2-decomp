# THUGPRO peer mode: chat, host options (155-159), change level name (2026-10-21)

NOT run in the game yet (and never online - by hand only). Built, `thug2_check` on THUG2PM.exe and the stock
THUG2.exe: 0 MISMATCHED, all early self-tests pass. The user's decisions: "We should fix chat. We should fix TOD."

## The mode
`GameNet::Thugpro::PeerMode()` (src/game/net/thugpro_peer.h): on when thug2-decomp.ini has
`[Miscellaneous] OnlineGameName=thugpro` or `[online] thugpro_peer=1`; off otherwise (THUG2's protocol unchanged).
**For the lead:** the join-path agent was to add this mode's switch; nothing was on main when this was written, so this
branch defines it (setting name `[online] thugpro_peer`, as the task said). If main gains its own switch, keep one:
`PeerMode()` / `SetPeerModeOverride()` (tests) and `MachineId()` (`[online] thugpro_machine_id=<hex>`, else random per
run) are the places to merge.

## What was done
- **Chat (76)**, TP-R2: in the mode we send THUGPRO's `MsgChatTHUGPRO` (machine id, object id, name 16, text <= 127;
  length 21 + text + 1) and read it back (`DecodeChat`: dropped when shorter than 22 bytes, longer than 149, a name or
  text not ended inside it, or text > 127 - THUGPRO's own drops). The chat handler **0x00532050** is decompiled
  (`GameNet::HandleChat`, chat.cpp, REPLACE): THUG2's layout as the original ("\c<colour + 2><name>\c0 : <text>" ->
  create_console_message), except that the name / text stop at the message's end and the line at 256 (the original
  sprintfs unbounded into 256 bytes). The relay 0x0056A000 forwards any length: nothing needed. chat.cpp's field copy
  no longer uses strncpy_s (a debug build filled the fields' rest with 0xFE - on the wire).
- **Host options**, TP-R3 / TP-R4 (thugpro_peer.cpp; registered by `SkateNet::AddClientHandlers` 0x0056EB50 - ours -
  in the mode, as THUGPRO's StartServer proxy: 155 / 159 flags 0, 156-158 late):
  - 155 server info (16 bytes: TOD state, weather, flags - bits 0-7 options, 8-15 pro sets 1-7 + G_COMP: THUGPRO's
    `bool : 1` fields) is kept and applied once the level is up (30 frames with the skater, again after every load of
    the session - a frame gap > 400 ms counts as a load - as THUGPRO keeps its flags): the time of day by THUG2's TOD
    manager (`SetTODInstant`, checked first by `CanSetTODInstant` - the original loops for ever on an unknown state;
    anything not one of the four is afternoon, as THUGPRO's `thugpro_change_time_of_day`); the weather on THUG2's
    weather object (0x006FA7C8: rain 16 / snow 5 / clear 0 - THUGPRO's `set_current_weather_values`; "default" leaves
    the level's own); each option **set** by `<level>_LEVEL_CHANGE_0n on` (the level structure's `level`: THUGPRO only
    ever sets its flags from 155 - an option it doesn't set stays as the level's setup made it, which in our ports runs
    THUGPRO's defaults); in THUGPRO's THPS4 levels and New York all 8 pro sets toggled as its `toggle_geo_nomenu`
    (the Kill / Create commands 0x005A5030 / 0x005A4FF0 by prefix, run from a spawned NullScript - Kill reads its
    script's object for some classes).
  - 156 / 157: the TOD / weather kept and applied at once when the level is up. 158: nothing (THUGPRO's handler is
    empty and nothing sends it). 159 (mods allowed != 1): we leave - ours aren't THUGPRO's scripts - through
    `SkateNet::HandleServerRemovedYou` as 'Q' (THUG2's "server removed you" dialog).
  - The TOD manager's multiplayer lock (ResetLevelLights 0x00573BE0, SetTODInstant 0x00574670, EnableBloom
    0x00574400 do nothing in multiplayer) is lifted in the mode's network games (`Gfx::SetMultiplayerUnlock`) - THUGPRO's
    patches 0x00573BF8 / 0x00574683 / 0x00574410; its 4th (0x00574375) only changes single-player games.
  - Hosting: before every start info (51) the server sends 155 (important queue, priority 255, group 8) from our TOD
    manager's state and the chat commands' options - App::EnqueueMessage's new filter (`Net::SetEnqueueFilter`).
    Chat commands in the mode: `/tod morning|afternoon|evening|night` and `/weather default|clear|rain|snow` (applied
    here, 156 / 157 broadcast when hosting), `/option <1-8> on|off` and `/proset <1-8> on|off` (applied here, sent with
    the next game's 155), `/thugpro` (machine id and the options). Clients get "only the host ...".
- **Change level (70)**, TP-R5: as host the 8-byte message goes out as 40 with THUGPRO's level name (its words for
  the skate shop / park editor / created park, else the level select's name - `Manager::LevelListName`, else
  "Unknown\n").

No original GameNet / Skate handler code was patched: the chat handler is now source; everything else hooks our own
source (net_app.cpp EnqueueMessage, skate_net.cpp AddClientHandlers, tod_manager.cpp, mouse_menus.cpp's frame).

## Not done
- Level options a THUGPRO host changes **during** a game: THUGPRO sends them as THUG2 flag broadcasts
  (`BroadcastOmnigon cheat_flag=SERVER_LEVEL_CHANGE_0n_ON`) with flag numbers from its own scripts; ours apply the
  host's options at the next game's server info.
- THUGPRO's chat mute list (by machine id), KillAllTextureSplats around its toggles (decals only), THUGPRO's per-level
  TOD scripts (`USE_GENERATED_LEVEL_TOD_SCRIPTS`: compiled without names - TPN-THAW-5's note) and weather settings.
- 159 as host (we have no mods setting), the scripts hash / join request (the join-path agent), LZHAM.

## Tests (src/hook/selftests_thugpro.cpp, Early)
- TestThugproLayouts: server info against THUGPRO's struct (its bool bit fields compiled here) and exact bytes; chat
  against its struct, offsets, cuts (127 / 15), lengths; decoding both layouts and every malformed case; change level
  against its derived class, the name cut, the names; the TOD states; the pro-set levels.
- TestThugproEffects: server info's effects in order on a recording stand-in (London: TOD, weather, options 1 3, all 8
  pro sets; a THUG2 level: no pro sets; no level structure; School's G_COMP ramp; single pro sets).
- TestThugproHandlers: 155-159 on a stand-in network manager (mode off, host, not a network game, local client, short
  messages, updates kept, 159's leave as 'Q' with the context untouched, session dropped when the game ends); the host's
  filter on a stand-in EnqueueMessage (155 before 51 with its queue / priority / group; 70 extended to 40 and taken
  over; other lengths, apps, messages, not hosting, mode off left alone); the TOD lock with 0x005651F0 stood in and the
  unlock installed; the walk check and the host's TOD from a stand-in manager.
- TestThugproChatHandler: 0x00532050 original vs ours on THUG2's layout (5 messages: signed colours, longest name /
  text, empty name / text; the C runtime's sprintf, the struct and RunScript stood in); an unended text; THUGPRO's
  layout shown and malformed ones dropped; sending in both layouts (SendToServer stood in); the client handlers'
  registration (Dispatcher::AddHandler stood in).
- Mutations (`node tools/mutate.ts` with the batch's spec - 58: layouts, decode bounds, server info's effects and
  order, the handlers' guards, the host's filter, the TOD unlock and walk check, the chat handler both layouts and the
  send): **58 / 58 caught**. Two first-run "missed" were equivalent (a chat text bound already implied by the message
  length; `length < header` vs `<=`) - the redundant text bound was removed; the TOD walk check is now bounded to one
  round of the four states (it could loop for ever on an unknown target when every state has its structure), with
  mutations on the bound, the target test and the final refusal.
- Audits (own snapshot copy): scan_register_assumptions - nothing new (its 5 RISK lines are AddVector / InitEditedGoal,
  not this batch); audit_game_function_calls - no MISMATCH (1537 uses); check_code_writes - "No overlaps".
- Not crash-checked in a workspace game: all four THUG2-n workspaces were held by other agents' locks.
- (Continued by a second agent after the first stopped: its uncommitted work reviewed - the weather offsets checked
  against 0x004A1190 / 0x004A1430 / 0x004A14A0 / 0x004A14B0, the 155 / 159 handling against thugpro_proxy's
  SkateNet.cpp / Application.cpp - built, checked, mutated, committed.)

## Switch it off
The mode is off unless set (above). To take the chat handler's source out: `[replacements] disable=chat.cpp` (that
also disables the rest of chat.cpp's replacements).

## For the user to try by hand (online - only by you)
1. Set `[online] thugpro_peer=1` (or `OnlineGameName=thugpro` for THUGPRO's lobbies). Offline first: free skate,
   open the chat box if it's offered and type `/thugpro` - it should answer with a machine id.
2. Join a THUGPRO player's free-skate session in a level both have (only once the join path works - another agent's
   work): chat both ways - their lines should read "<name> : <text>" with their colour, ours reach them unshifted.
3. Ask the THUGPRO host to change the time of day (its server menu) - ours should switch at once (156). A night level
   start: ours should match the host's TOD after loading (155). Weather rain / snow likewise.
4. In a ported level with options (THAW Downtown / Minneapolis, London, Chicago, Zoo...): the host turns an option on,
   starts a new game - ours should build it (e.g. London's bike race, Minneapolis' garage). THPS4 levels: pro sets.
5. Host a game for a THUGPRO player: they should see our level's name in their change-level message, our `/tod night`
   should reach them; `/option 1 on` then a new game should switch their option.
6. Read thug2-decomp.log: "thugpro: server info: ..." and "thugpro: host options applied in <level> ..." lines.
