# Pending: menu input mapping (Mdl::FrontEnd), 0x0054EF60-0x0055002F (branch wt/menu)

- [x] Batch (NOT run in the game yet): **Mdl::FrontEnd**, the "Menu input mapping" subsystem
  (src/game/front/front_end.{h,cpp}) and the **Mdl::CCompetition** pieces the linker put after it
  (src/game/skate/competition.{h,cpp}). 20 functions; 0x0054FFE0 (a jump to Str::String's destructor) already had source.
  - FrontEnd (0x164 bytes, vtable 0x0064D724; instance 0x007CE464, references 0x007CE468): AddKeyboardHandler
    0x0054EF60, UpdateButtonEventMappings 0x0054EF80, KeyboardTask 0x0054F200, Update 0x0054F380, GrabControlEvent
    0x0054F600, SetAnalogStickActiveForMenus 0x0054F900, PauseGame 0x0054F960, InputLogic 0x0054FB40, LogicTask
    0x0054FCC0 (task code Ghidra never made a function; `data/derived/missed_functions.txt` doesn't list it either),
    UpdateInputMappings 0x0054FD00, constructor 0x0054FE00, CreateFrontEnd 0x0054FF80; script commands
    SetMenuAutoRepeatTimes 0x0054F0F0, SetMenuPadMappings 0x0054F150, SetButtonEventMappings 0x0054F1B0,
    SetAnalogStickActiveForMenus 0x0054FDC0.
  - CCompetition (Mdl::Skate +0x360): Leader constructor 0x0054FFD0 (a Str::String name in a 0x20-byte leader,
    8 of them at +8), destructor body 0x0054FFF0 (the names, last first), EditParams 0x00550010
    (StartCompetition: params appended to the competition's), StartRun 0x00550020 (StartCompetitionRun).
  - Still in the game (FrontEnd, before the range): destructor 0x0054EE70, v_start_cb 0x0054EEA0, v_stop_cb 0x0054EF30,
    the auto-repeat of held directions 0x0054ECE0, Release 0x0054EBE0.
- What the mapping does (names found by CRC-guessing: inactive/active/use_as_first, block_menu_input, pad_start/
  pad_back/pad_choose, keyboard_anchor/current_menu_anchor/dialog_box_anchor, device_num, controller,
  handle_keyboard_input):
  - SetMenuPadMappings [m0 m1]: per controller inactive=0, active=1 (default), use_as_first=2, anything else 0. Only
    zero/non-zero is used here: an inactive controller's pad events are dropped.
  - SetButtonEventMappings xbox=[[button event]...]: buttons are indexes into the Xbox names (left_trigger_full,
    right_trigger_full, left/right_trigger_half, y b a x back, left/right_stick_button, 5 unused "null"s, black,
    white). Up to 32 (button, event) pairs. Quirk kept: button `null` removes the event's pairs but then the event
    still takes a free slot as button 0; event `null` removes the button's pairs, then (button, null) takes a slot.
    block_menu_input / unblock_menu_input stop / restart every pad event.
  - InputLogic each frame per controller: the auto-repeat event (0x0054ECE0) and the n-th pressed pair's event,
    looping while there are events, each launched through the tracker (0x00462F00, type/target 0x36B2EE74) with
    device_num and controller=0 (the original loads the mapping, then passes 0). Start (bit 0x800) is pad_start;
    with the on-screen keyboard up, controller 0 also gets Escape -> pad_back (a menu up) / pad_start, Enter/Space ->
    pad_choose (a menu or dialog up; Space with anchor 0x487FBC86 up returns 0 and leaks its params - kept).
  - SetMenuAutoRepeatTimes (delay rate) seconds -> ms (truncated); defaults 300 / 50.
  - The PC port's UIControls setting (src/game/sio/sdl_input.*): nothing here reads it - these functions take their
    mappings only from the scripts' calls, so they are not what it was meant for (as far as this code shows; the
    scripts that call SetButtonEventMappings might be). Behaviour unchanged.
- Test (src/hook/selftests_menu.cpp, 8 Early tests): originals vs ours with every callee stood in for by recording
  code (structure calls, pools, scripts, keyboard, devices, input manager, skate module, tracker, Calloc...), traces
  plus the front end's bytes and globals compared. 66 deliberate bugs, all caught (65 at once, 1 after adding a
  negative-index case to GrabControlEvent's test).
- Checks: 0 MISMATCHED, all early tests pass, register scan (only the known 0x4EFF92 risk; CreateFrontEnd returns
  EAX), call audit (only "?" for tail-jumping callees 0x00406C90 / 0x0047B000 / 0x00564D60, checked by hand: RET 4 /
  RET / RET), check_code_writes "No overlaps", find_gaps nothing new near the range, 0 thunks.
- For the merger: new directories src/game/skate/ and new CMake entries (front_end.cpp, competition.cpp,
  selftests_menu.cpp). Calls into the other agents' ranges go through GameFunction: tracker 0x00462340 (ObjectExists),
  0x00462B70, 0x00462F00 (wt/trk) and 0x0045F5D0 (wt/evt). Script globals 0x007CE46C-0x007CE46F (menus active, a
  skateshop override, keyboard up, menu up) and 0x007CE4A0 are FrontEnd.cpp statics.
