# Online & network game modes, the end: profiles, buddies, content vault, stats 0x00548560-0x0054EF60 - NET4 agent

Branch worktree-agent-a2e095525737a9b1f. Tests in src/hook/selftests_net_buddies.cpp (TestNetBuddyLists,
TestNetBuddyMenus, TestNetBuddyCallbacks), on GameNetKit's arena with more stand-ins in src/hook/net4_testkit.h
(script structures whose getters answer from tables, preferences, script arrays, the C runtime the originals call -
strtok / atol / strchr / strupr done by ours -, the clock, tasks, threads; our callbacks mapped to the original's
addresses before comparing). Nothing goes online: GameSpy's answers are made up in the tests.
Shared files touched: CMakeLists.txt (new files listed).

- [x] Batch NET4-1 (NOT run in the game yet; the online profile and buddy list - GameNet::BuddyMan, 37 functions):
  src/game/net/buddy_man.h / .cpp, src/game/net/online_text.h (new: strcpy / strtok with the original's semantics).
  - The class (manager +0x18, 0x3C bytes; the GP connection is the object's own address): 0x0054A480 constructor
    (gpInitialize product 0x2880, the six callbacks - two of them the empty 0x005480A0 -, Tsk::Task<BuddyMan> on
    the network heap), 0x005488B0 destructor body (the task deleted through its vtable; only the heads unlinked),
    0x0054A1A0 LogIn (log off first, state 2, the login thread 0x00548140 - NET3's range -, the task added once),
    0x0054A5C0 CreateProfile (state 1, CreateProfileThread; the task added without the InList check LogIn has),
    0x00549B90 LogOff, 0x00548D50 AddBuddy (a pending buddy, gpSendBuddyRequest with ""), 0x00549A50 RemoveBuddy,
    0x005499A0 CancelAddBuddy (the first pending one only), 0x00549CB0 IsBuddy, 0x00548E10 CountBuddies,
    0x005498E0 FillBuddyList, 0x00549F50 FillProspectiveBuddyList (players with a profile, not local, not
    buddies), 0x00548560 ShowBuddy (stdcall; homie_list_add_item; the pad_choose_params structure is never freed -
    kept), the GP callbacks 0x00549ED0 OnError (0x107 / 0x201 / 0x202 / 0x603), 0x00548F50 OnBuddyStatus (an
    answer to our request goes into the buddy_array preference: the array copied entry by entry plus the new one;
    someone new gets the nick "ProSkater" or buddy_array's), 0x005496F0 OnBuddyMessage (dropped past 20 waiting;
    "nick\ntext", or a buddy's one-line text - a buddy nick of 16+ characters runs into the text: kept),
    0x0054A2C0 OnConnectNewUser, 0x0054A300 BuddyTask (10 s login limit, GP processed once a second when logged
    in), 0x0054A3D0 CreateProfileThread (gpConnectNewUser "proskater" with the email / password preferences).
  - Buddy (0x24C) / BuddyMessage (0x68) list nodes: 0x00548CB0 BuddyMessage::Construct, 0x00548CF0 /
    0x00548D20 their scalar deleting destructors.
  - CFunctions: 0x00548940 SetUniqueId (the time as text into unique_id), 0x00548A40 JoinBuddy (the buddy behind
    our public address is joined at its private address on port 0x1421, else a random 0xFFFF value is added),
    0x00548E40 AddBuddy, 0x00548E90 HasBuddies, 0x00548ED0 BuddyListFull (30), 0x00548F10 AnyBuddyMessagesLeft,
    0x00549D10 ProfileLogOff, 0x00549D20 FillBuddyList, 0x00549D80 CancelAddBuddy, 0x00549D90 RemoveBuddy,
    0x00549DC0 ShowNextBuddyMessage (into the script's own parameters), 0x00549EA0 IsAlreadyMyBuddy, 0x0054A250
    ProfileLogIn (profile_success must be boolean_true), 0x0054A2B0 FillProspectiveBuddyList, 0x0054A650
    CreateProfile.
  - Found 3 functions Ghidra hadn't made (callbacks only reached by address): 0x00549ED0, 0x0054A2C0, 0x0054A300,
    and the thread 0x0054A3D0.
  - Original quirks kept and noted: nicks and URLs used as sprintf formats (ours print them with "%s": a '%' in a
    nick isn't expanded); IsAlreadyMyBuddy's uninitialised profile when the parameter is missing (here 0); the
    location parse copies from address 0 when a THUG2 location has fewer than four ':' (a crash in both).
  - Tests: every list operation over lists of 0-3 buddies (pending ones anywhere), the counts at 0/1/2/29/30/31,
    the commands, constructors and destructors (flags 0/1/3); ShowBuddy over status x join x remove; the
    prospective list (local, no profile, buddy, two more); JoinBuddy (observe x same address); every GP error;
    13 buddy-status cases (found first / second, pending, playing in a THUG2 game / elsewhere, new with / without a
    buddy_array match, no array); messages at the limit, one or two lines, from a buddy or not; the new user's
    answers x the flag; the task over every state and the timer edges; log in / create profile; ProfileLogIn.
    Untested without a live connection: what GameSpy really answers (GP's structures are filled by hand here).
    Mutations (out/net4/mut_buddies.ts): 44, 43 caught; the miss (OnBuddyStatus' location parse with the
    unchecked strchr chain) differs only where both crash.
- [x] Batch NET4-2 (NOT run in the game yet; the content vault and HTTP transfers - GameNet::VaultMan, 38 functions):
  src/game/net/vault_man.h / .cpp (new); gamenet_manager.h: the manager's +0x14 named m_vaultMan (was m_unknown14).
  - The class (manager +0x14, 0x3C8 bytes; www.thugonline.com: <folder>/dir_list.txt the categories,
    <folder>/<category>/dir_list.txt their items, cgi-bin/upload2.pl the uploads): 0x0054C1D0 constructor (the
    task, a semaphore of 1/10), 0x0054B470 destructor body, 0x0054A910 Download (thiscall, `this` unused: the
    manager's object is used; the top stats download through it too - stats.cpp's StatsRequest is this
    DownloadRequest: its "timeout" is the buffer size, +0x108..0x114 the fail / done / progress / error scripts),
    0x0054B370 StartList, 0x0054B400 ListItems, 0x0054B500 ListCategories, 0x0054C130 FreeDirectoryListing,
    0x0054B6E0 VaultTask (25 s without progress cancels; the progress script with "%d%%" for downloads; the lists
    parsed line by line - categories and items are put at the front, so they end up in reverse order; an item line
    is file:name:creator[:size:pieces:gaps:goals:theme:tod for parks | :score tricks | :is_male skaters |
    :num_goals goals]:<field>:description; after the last category, net_vault_menu_create; an empty category list
    leaves the transfer as it is - read again each frame), the threads 0x0054A7B0 ListThread (100000-byte zeroed
    buffer), 0x0054A860 DownloadThread (frees the goastrdup'd URL with the game runtime's free), 0x0054AAC0
    UploadThread (the data structure written with WriteToBuffer, its caseless CRC as "checksum", the type's fields;
    a trick / skater / goal gets "filetype" twice), the ghttp callbacks 0x0054A660 OnProgress (received / total *
    100 truncated, x87 order kept: 29 / 100 gives 28), 0x0054A740 OnTransferDone, 0x0054A6B0 OnFileDownloaded
    (0x005A8EB0 with the type's script).
  - Items (0x214) / categories (0x44) / their searches: 0x0054B570 / 0x0054B670 constructors, 0x0054B5E0 /
    0x0054C980 scalar deleting destructors, 0x0054C890 the category's destructor body, 0x0054B2B0 / 0x0054B2D0
    the node destructors, 0x0054B2F0 / 0x0054B330 / 0x0054B610 their scalar deleting ones, 0x0054B6B0 /
    0x0054B640 the search's First / NextItem, 0x0054A760 / 0x0054A770 / 0x0054A780 / 0x0054B280 the searches'
    destructors.
  - CFunctions: 0x0054AA10 DownloadFile, 0x0054B060 UploadFile, 0x0054B240 CancelFileTransfer, 0x0054C290
    DownloadDirectoryList (profile 0x011B0C16 - one particular GameSpy profile - first gets the uploads list as an
    "Uploads" category), 0x0054C460 FreeDirectoryListing, 0x0054C470 FillVaultMenu, 0x0054C770 / 0x0054C800
    Next / PrevVaultCategory (into the script's own parameters).
  - Found 4 functions Ghidra hadn't made: 0x0054A660, 0x0054A6B0, 0x0054A740 (ghttp callbacks), 0x0054AAC0 (the
    upload thread).
  - Original quirks kept and noted: URLs and file names as sprintf formats (ours "%s"); lines with too few ':'
    write through null (strstr unchecked); UploadThread's file name is an unset local for an unknown type (here
    empty; not compared by the test); the item's URL is sprintf'd without a limit (ours bounded to the item's URL
    and name fields).
  - Tests TestNetVaultLists / TestNetVaultTransfers / TestNetVaultTask (selftests_net_vault.cpp): every node /
    search / destructor function with flags, the walk over 0-3 categories, next / previous from each category, the
    menu for every type and 0-2 items; Download, DownloadFile, lists, DownloadDirectoryList (each type x the uploads
    profile), UploadFile, Cancel, the three threads (each type), OnProgress over states and totals (0, negative,
    past 32 bits, x87 rounding), the completed callbacks (results 0 / 1 / 11 / -1, each type); the task: timeouts,
    progress, downloads / uploads x result x error, the category lists (lines, one, blank, failed, skipped first),
    the items of each type x next / skipped / last category, the uploads list. Untested without a live
    connection: thugonline.com is gone - what its lists looked like is inferred from the parser.
    Mutations (out/net4/mut_vault.ts): 52, 50 caught at first, 52 after strengthening (x87 rounding case, a
    negative ghttp result).
