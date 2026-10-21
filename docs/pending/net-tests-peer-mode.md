# Two early self-tests that failed only in the game: the THUGPRO peer mode

- [x] "skate net registration" (selftests_tod4.cpp) and "net compress message" (selftests_netD.cpp) failed in game
  runs in the workspaces C:\Games\THUG2-1 / -3 / -4 but passed offline in thug2_check. Cause: those workspaces' ini
  has `[Miscellaneous] OnlineGameName=thugpro`, and `GameNet::Thugpro::PeerMode()` (thugpro_peer.cpp) turns the
  THUGPRO peer mode on for that game name. With it on, ours (as intended) registers the peer mode's client handlers
  155-159 (0x9B-0x9F) at the end of SkateNet::AddClientHandlers, and CompressMessage sends LZSS as LZHAM (TP-R11) -
  the tests compared that with the original. thug2_check reads no ini: the mode is off there, so they passed.
  Not a behaviour difference: with the peer mode off, both match the original exactly (THUG2's protocol unchanged).
- Fix (tests only, no game code changed): both tests force the mode off (`SetPeerModeOverride(0)`, put back to the
  settings in their clean-ups) for the original-vs-ours comparison. "skate net registration" then also runs ours'
  AddClientHandlers (clients 0 and 1) with the mode off and on, and checks that "on" is exactly "off" plus the
  five peer-mode registrations (155, 159, 156, 157, 158: our handlers, their packet flags, the skate module, priority
  0x80) after it. LZHAM with the mode on stays selftests_thugpro_join.cpp's.
- [x] thug2_check on THUG2PM.exe and the stock THUG2.exe: 589 early self-tests passed, argument sizes 0 mismatched;
  check_code_writes: no overlaps.
- [x] Game run: C:\Games\THUG2-2 with OnlineGameName=thugpro set for the run (offline, nothing online), -FullTests
  -Level BO: early 589 / late 17 / engine-started 3 all passed (both tests and "skate net handlers" included),
  3831 of 3831 replacements installed, BO free skate up, no EXCEPTION / RUN-TIME CHECK FAILURE / DEBUG CRT REPORT.
