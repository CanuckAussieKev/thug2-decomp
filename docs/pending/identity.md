# Online identity: [Miscellaneous] OnlineGameName / OnlineGameKey (agent "identity", 2026-10-20)

Not a decomp batch (no game function decompiled). Test online only by hand; tools and agents never go online.

## What identifies the game to GameSpy / OpenSpy (THUG2PM.exe = stock no-CD THUG2.exe = THUGPro.exe, same layout)
- **Game name "thps6pc"**: plain text at 0x0064B1CC, pushed as an address by WinMain's availability check (0x004E2694,
  rewritten: winmain.cpp), qr2_init (push at 0x0053BE86; 0x005FB230(NULL, NULL, 5155, name, key, 1, 0, callbacks...)),
  the qr2 server key callbacks' "gamename" key (0x0053BA1F, 0x0053F936 -> 0x005FA010), peerSetTitle (0x005FCF30:
  title 0x00542FA0, server-browsing name 0x00542F8B) and the stats SDK's game name (strcpy at 0x0054CAD9 into
  0x007D7A78). Nothing else refers to 0x0064B1CC.
- **Secret key "AdLWaZ"**: bitwise-NOT, 6 bytes, at 0x0067F120 (decoded onto the stack for qr2_init, 0x0053BE27..) and
  at 0x0067F154 (for peerSetTitle's two keys, 0x00542F33..); plain at 0x0064D58C (strcpy at 0x0054CAE8 into 0x007D7B78,
  the stats SDK's key). Always 6 characters (the decoders copy exactly 6).
- Not identity overrides (unchanged, THUGPRO leaves them too): the service task's second availability check with
  "mototrax" (0x0064CDF0, Net::ServiceTask - the SDK keeps the last checked name at 0x007DB640 and uses it in NAT
  negotiation, GP logins (0x00617400...) and the MOTD URL's gamename=%s), the MOTD product id 10368 (0x00545673), the CD
  key SDK's game id 1003 (gcd_init 0x005F9540, its host key.gamespy.com built on the stack - not covered by
  OnlineDomain either), GP's "10thug211:..." location strings (0x0064C852, 0x0064CD76, 0x0064CEBC, 0x0064D120 - plain
  text; the bitwise-NOT text near 0x0064CEB0 is a registry path, "...und\Install"), the stats path "thug2pc/%s".

## What THUGPRO's thugpro.dll overrides (from its distributed binary, 0x1001BC70: memcpy over the exe's strings)
- The host names in place (gamestats, %s.available, %s.master, natneg1/2, peerchat, %s.ms%d, gpcm -> *.openspy.net),
  the game's own news URL 0x0064CD8C -> http://thpsx.com/motd.dat.
- **"thugpro" (8 bytes) over "thps6pc" at 0x0064B1CC** - the game name, so every use above.
- "openspy" (8 bytes) over 0x007D6BB0: the CD key SDK's XOR key, which gcd_init (0x005F8AD0) sets to "gamespy" itself
  before any use - so it has no effect.
- Not the secret key: THUGPRO logs in with THUG2's AdLWaZ. (Also timeouts: 0x005F88F9 2000 -> 20000 ms, 0x005FBAF5
  10000 -> 30000 ms - not identity.)

## The settings (src/game/net/online_identity.{h,cpp})
- `OnlineGameName=` (1-31 letters/digits) and `OnlineGameKey=` (exactly 6 characters); empty = THUG2's own
  (thps6pc / AdLWaZ, OpenSpy's "Beenox" lobby); invalid values are ignored with a pc: warning in the log.
- THUGPRO: `OnlineGameName=thugpro`, OnlineGameKey empty (OpenSpy lists its lobbies "General" and "Newbies").
  Joining THUGPRO players' sessions may not work: their scripts and DLL differ.
- Run-time writes (only when a setting differs from the game's own; SetUpOnlineIdentity in InitPcSetup): the name's
  address over the 6 push operands above, the key over its 3 copies (data). WinMain reads Net::GameName() itself.
  tools/check_code_writes.py: 26 writes (was 17; now with sizes), no overlaps. CLAUDE.md and progress_report.py say so.
- Log line: `pc: ONLINE IDENTITY: game name <name>[ (written)], key ...`.

## Tests
- Self-test TestOnlineIdentity (selftests_pc.cpp, Early): the sites' original bytes (push 0x0064B1CC, the NOT-ed and
  plain key), then a name + key written and read back raw, a bad key refused, only-name / only-key, restored. 10
  mutations of online_identity.cpp all caught (THUG2_SELFTESTS=OnlineIdentity).
- WinMain's self-test (selftests_winmain.cpp): the original's 0x0064B1CC stands for Net::GameName(), so it passes with
  the setting on; mutation (another name in WinMain) caught.
- thug2_check: 301/301 on THUG2_PM_BAK\THUG2PM.exe and on C:\Games\THUG2\Game\THUG2.exe.
- Game run (THUG2-2 under its lock, offline: -level BO free skate, no menus, no network): with OnlineGameName=thugpro
  and a test key, all self-test stages passed and the log shows both written. Ini and DLL put back.
- **Online: untested** - for the user, by hand.

Shared files touched: CMakeLists.txt (one source line), CLAUDE.md, tools/check_code_writes.py, tools/progress_report.py,
src/game/sys/pc_setup.cpp, src/game/sys/winmain.cpp, src/hook/selftests_pc.cpp, src/hook/selftests_winmain.cpp.
