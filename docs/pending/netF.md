- [x] Batch netF, engine net library 0x455970-0x456910: Net::Conn (branch wt/netF, 41 functions incl. three pieces
      Ghidra never made functions; **NOT run in the game yet**): src/game/net/net_conn.* - Conn's constructor
      0x455C80 and destructor 0x456790, GetSendInterval 0x455970 (4 bytes: REPLACE_SOURCE_ONLY),
      Set/GetForceSendThisFrame 0x455980 / 0x455990, GetBandwidthType 0x4559A0, Set/GetBandwidth 0x4559B0 /
      0x4559C0, IsForeign / IsLocal / IsRemote 0x455A10-0x455A30 (flags +0x64: 4 / 1 / 2), SetBandwidthType 0x455A40
      (0 broadband 400000, 1 modem 4200, 2 LAN 800000 bytes/s; any other type keeps the bandwidth), GetAveLatency
      0x455B40 (0 for a local connection), Invalidate 0x455C10 / IsValid 0x455C20 (status +0x60: bit 0 ready, bit 2
      invalid), GetResendThreshold 0x455C30 (status bit 1: 1000 ms; else twice the average latency, at least 500,
      signed), DestroyReceivedSequencedMessages 0x455E50, DestroyLinks 0x455F30 (every link of the three queues and
      the 256 received queues; the two stream lists with their StreamDescs), DestroyImportantMessages 0x456210,
      ReleaseQueuedMessages 0x456370 (reference counts only, links kept), GetHighestResendCount 0x456540 (netC called
      it GetNumPendingMessages: it's the most resends of any important / sequenced message),
      GetNumQueuedStreamPieces 0x4565E0, ClearResendCounts 0x456630, FlagMessagesForResending 0x4566D0,
      DestroyMessageQueues 0x456770; the list types of the range (net_conn.h): Lst::Search<T> destructor bodies
      0x4559D0-0x455A00 and scalar deleting destructors 0x455A80-0x455B10 (vtables 0x647564 MsgSeqLink, 0x647568
      MsgLink, 0x64756C MsgImpLink, 0x647570 StreamLink), Lst::Node<T> destructor bodies 0x455B50 / 0x455B70 /
      0x455B90 / 0x455BB0 (vtables 0x6474CC / 0x6474D0 / 0x6474D4 / 0x6474C8), Lst::Node<Conn>'s deleting destructor
      0x455BD0 (ConnNode::Destroy, net_lists.cpp), Lst::Head<MsgSeqLink>'s constructor **0x455C60** (not a Ghidra
      function: the received queues' array-constructor callback); and batch 85's two by-address pieces:
      **0x457010** Lst::Head<MsgHandler>'s constructor (MsgHandlerNode::ConstructHead: 0x457010-0x45702C, vtable
      0x6475E8) and **0x456CA0** App::ProcessNetworkData (0x456CA0-0x456CB8: ++frames (+0x1410), tail jump to
      vtable slot 1 ProcessData - "not checked" by thug2_check, like 0x4593C0).
      Conn (0x3D20): three queues +0x00 / +0x14 / +0x28 (head vtables 0x647578 / 0x64757C / 0x647580, folded with
      the links' own), handle +0x3C, ip +0x40, running id +0x44 (global 0x67D800), port +0x48, write buffer
      +0x4C/+0x50 (0x514), read buffer +0x54/+0x58 (0xA28), +0x5C never touched, status +0x60, flags +0x64, App
      +0x68, last comm / send time +0x6C / +0x70, send interval +0x74, outgoing sequence ids per group +0x78,
      expected per group +0x478, next stream id +0x878, received queues +0x87C (256), received streams +0x1C7C,
      sent streams +0x1C90 (vtable 0x647584), LatencyTest +0x1CA4, Metrics in / out +0x1CDC / +0x2CE8 (0x100C
      each, wt/netD), force-send +0x3CF4, app node +0x3CF8, alias +0x3D0C, bandwidth type +0x3D10, last timestamp
      in +0x3D14, timestamp out +0x3D18, bandwidth +0x3D1C (400000 from the constructor).
      Quirk kept: the constructor zeroes only the first 64 groups' sequence ids (REP STOSD of 0x40 dwords, both
      arrays); the type stays uninitialised (+0x3D10).
      Calls into the other ranges by address: Metrics' constructor 0x4544E0 and ~StreamDesc 0x454E00 (wt/netD),
      ~QueuedMsg 0x4550F0 (wt/netE). Existing callers now call Conn directly (net_app.cpp, net_app_view.cpp,
      net_access_c.h; App::Construct uses ConstructHead and ProcessNetworkData); netA / netB / netC's tests stand in
      for ours next to the game addresses (0x455C80, 0x456790, 0x4566D0, 0x456540) and netB's arena relocates
      &App::ProcessNetworkData to 0x456CA0.
      Test (selftests_netF.cpp, TestNetConn, Early): four connections built by the original / our constructor on
      dirty memory in a 64 KB-aligned arena with a stand-in heap, filled through the game's own link constructors
      (shared messages with reference counts, resend counts, packetstamps, stream pieces, received messages, stream
      descriptors in place or not), every getter / setter over flag, status and latency tables, every queue
      function and the destructor, the list types and the process task; the whole arena and every result compared.
      56 mutations: 54 caught at once (one rewritten: a read-buffer size the heap's rounding hid), 1 after
      strengthening (DestroyLinks on a connection with received links left), 1 equivalent (`<` vs `<=` against the
      500 ms minimum).
      Disable with `[replacements] disable=net_conn.cpp` (net_lists.cpp and net_app.cpp hold 0x455BD0 / 0x457010
      / 0x456CA0).
