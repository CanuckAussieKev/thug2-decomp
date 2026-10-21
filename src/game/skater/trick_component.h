#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

// The trick component (Trick, type 0x270F56E1; made by 0x00523490 - the neighbouring range): the skater's button
// events and the trick checks over them. Every frame the pad's buttons are turned into events (pressed / released,
// with the time) in a ring of 100; a trick's trigger (Press, Tap, Release, InOrder, TripleInOrder, AirTrickLogic...)
// is matched against the newest events within its time, and the events it used are marked so they aren't used
// twice. This part (0x0051EA40-0x00521B90) holds the event ring, its checks and the trick lists' queue; the rest of
// the component is in the next range. Only the fields used here are named; the others are reached by offset.
namespace Obj {

// A button event (0x18 bytes, 100 of them from +0x38; the newest at m_eventHead).
struct TrickEvent {
    uint32_t time;      // +0x00 Tmr::GetTime() when it happened
    int type;           // +0x04 1 pressed, 2 released
    uint32_t button;    // +0x08 the button's checksum
    uint32_t unknown0C;
    bool used;          // +0x10 used by the trick being checked (cleared or turned into `flags` after)
    uint8_t pad11[3];
    uint32_t flags;     // +0x14 used by a trick of these kinds (the checks' masks)
};
static_assert(sizeof(TrickEvent) == 0x18 && offsetof(TrickEvent, used) == 0x10 && offsetof(TrickEvent, flags) == 0x14,
              "layout must match the original");

// A queued trick (0x14 bytes, 100 of them from +0xA34): which list and which of its tricks.
struct QueuedTrick {
    uint32_t list;      // +0x00 the trick list's name (0: removed)
    int index;          // +0x04 its index in the list
    uint32_t unknown08[2];
    bool flag;          // +0x10
    uint8_t pad11[3];
};
static_assert(sizeof(QueuedTrick) == 0x14, "layout must match the original");

class CTrickComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x270F56E1;
    static constexpr int kEvents = 100;
    static constexpr int kButtons = 22;

    // 0x0051EA90 (not a Ghidra function; registered with the composite object manager): its script member
    // functions (the table at 0x00683148: Pressed, GetCurrentBailEndpose, SetCurrentBailEndpose).
    static void __cdecl RegisterMemberFunctions();

    // 0x0051F2B0 (vtable Finalize): the skater's input, balance trick, core physics, flip and rotate, state and stats
    // manager components.
    void Finalize();
    // 0x0051F320: everything cleared (the events, the queue, the lists, the buttons' state).
    void Reset();
    // 0x0051EAB0 / 0x0051EB30 / 0x0051EF00: the extra trick lists / the +0x1238 block / the +0x1280 block cleared.
    void ClearExtraTricks();
    void ClearBlock1238();
    void ClearBlock1280();
    // 0x0051EB00: every event emptied (type, used, flags; the times and buttons stay), none counted.
    void ClearEventBuffer();
    // 0x0051EB80: the used marks of the counted events cleared.
    void ClearUsed();
    // 0x0051EBD0: the counted events marked used get `flags` and lose the mark.
    void MarkUsed(uint32_t flags);
    // 0x0051F570: a new event of `type` at the head (the oldest dropped when there are 100): now, unused, no flags.
    TrickEvent* AddEvent(int type);
    // 0x0051F610: the events of `button` older than `ms` get every flag (no trick uses them any more).
    void ExpireButton(uint32_t button, uint32_t ms);
    // 0x0051F6B0: a press of `button` within `ms` not used by `mask`: marked used, true.
    bool FindPress(uint32_t button, uint32_t ms, uint32_t mask);
    // 0x0051F7A0: presses of both buttons within `ms` (no other press between them): marked used, true.
    bool FindPressBoth(uint32_t first, uint32_t second, uint32_t ms, uint32_t mask);
    // 0x0051F8F0: button `index` pressed or not this frame: a held-off button (+0x9A8, until a time) stays off; a
    // change becomes an event (unless the button is ignored, +0xA1C).
    void UpdateButton(int index, bool pressed);
    // 0x0051F290: button `index` off until `until` (ms), and up.
    void HoldOffButton(int index, float until);
    // 0x0051F9B0: the spin sequence of `buttons` (the second pressed, then within two events the first, then the
    // third - or with `any` anything): within a second; marks the events (0x100), sets the button map.
    bool CheckSpinSequence(const uint32_t* buttons, bool any);
    // 0x0051FB90: `button` released then pressed (with `twice`: Triangle, Circle or Square released, pressed, then
    // `button` pressed) within a second; with `setMap`, all events marked and the button map set from the
    // arguments themselves (as the original does).
    bool CheckReleasePress(uint32_t button, bool twice, bool setMap);
    // 0x0051EC40: `count` buttons of a trigger (its components from `node`) into `out` (the three generic names
    // mapped through the button map), then the trigger's time: its number, or the global integer it names.
    uint32_t ReadTrigger(const uint8_t* node, int count, uint32_t* out);
    // 0x0051ECC0: the button map (+0x99C) set.
    void SetButtonMap(const uint32_t* buttons);
    // 0x0051ECF0: whether `button` is down.
    bool IsButtonDown(uint32_t button);
    // 0x0051ED10: the trick queue emptied. 0x0051ED20: a trick queued (dropped when the queue is full).
    void ClearQueue();
    void QueueTrick(uint32_t list, int index, bool flag);
    // 0x0051ED90 (not a Ghidra function): the trick list `list` taken out of the extra lists (the one after a removed
    // one is skipped, as in the original) and out of the queue.
    void RemoveTrickList(uint32_t list);
    // 0x0051EE10: whether `trick` is one to check: a direct trigger, or a name of the trick lists naming a structure
    // or an integer.
    bool IsTrickEntry(Script::CStruct* trick);
    // 0x0051EF50: whether `trick`'s params name `name` (case-insensitively).
    bool TrickNamed(Script::CStruct* trick, const char* name);
    // 0x0051F010: the newest 10 events printed (compiled-out debug output).
    void DumpEventBuffer();
    // 0x0051FDA0: whether the trigger `trigger` (its first component the kind - Press, Tap, InOrder... -, then its
    // buttons and time) was done: matched against the newest events within its time, not by events with any of
    // `mask`'s flags. When it is, its events get `markFlags` and (mostly) the button map is set; when the events ran
    // out, the used marks are cleared. With `scaled`, a Press's time is stretched by the core physics' rate (+0x2E8)
    // below 1.
    bool CheckTrigger(Script::CStruct* trigger, uint32_t markFlags, uint32_t mask, bool scaled);
    // 0x00521930: the tricks of the list `list` whose trigger (or alternate trigger) was done are queued.
    void QueueDoneTricks(uint32_t list, bool flag, uint32_t used);
    // 0x00521A10: the manual tricks list (+0x1230, when the balance component's +0x74) and the extra lists checked.
    void QueueAllDoneTricks();
    // 0x00521A80: the first done trick of the list `list` becomes the pending one (+0x126C...).
    void SetPendingTrick(uint32_t list, uint32_t mask, bool flag);

    // --- script member functions (the table at 0x00683148) ---
    // 0x0051F260 SetCurrentBailEndpose (not a Ghidra function): the unnamed checksum (0 without one). True.
    bool SetCurrentBailEndpose(Script::CStruct* params, Script::CScript* script);

    uint32_t m_unknown18;          // +0x18
    uint8_t* m_balance1C;          // +0x1C (+0x74: manuals checked)
    uint8_t* m_input;              // +0x20 (+0x498 / +0x499: no tricks)
    CBaseComponent* m_balanceTrick;  // +0x24
    uint8_t* m_corePhysics;          // +0x28 (+0x2E8: a rate, the trick windows' stretch below 1)
    CBaseComponent* m_flipRotate;    // +0x2C
    CBaseComponent* m_skaterState;   // +0x30
    CBaseComponent* m_statsManager;  // +0x34
    TrickEvent m_events[kEvents];    // +0x38
    uint16_t m_eventCount;           // +0x998
    uint16_t m_eventHead;            // +0x99A
    uint32_t m_buttonMap[3];         // +0x99C what the generic button names stand for
    float m_heldOffUntil[kButtons];  // +0x9A8
    bool m_down[kButtons];           // +0xA00
    uint8_t padA16[2];
    int m_ignoredCount;              // +0xA18
    uint32_t m_ignored[2];           // +0xA1C
    bool m_unknownA24;               // +0xA24
    uint8_t padA25[3];
    Script::CStruct* m_trickLists;   // +0xA28
    int m_queueFirst;                // +0xA2C
    int m_queueLast;                 // +0xA30
    QueuedTrick m_queue[100];        // +0xA34
    int m_extraListCount;            // +0x1204
    uint32_t m_extraLists[10];       // +0x1208
    uint32_t m_manualList;           // +0x1230
    uint32_t m_listUsed;             // +0x1234
};
static_assert(offsetof(CTrickComponent, m_input) == 0x20 && offsetof(CTrickComponent, m_events) == 0x38 &&
                  offsetof(CTrickComponent, m_eventCount) == 0x998 &&
                  offsetof(CTrickComponent, m_buttonMap) == 0x99C &&
                  offsetof(CTrickComponent, m_heldOffUntil) == 0x9A8 && offsetof(CTrickComponent, m_down) == 0xA00 &&
                  offsetof(CTrickComponent, m_ignoredCount) == 0xA18 &&
                  offsetof(CTrickComponent, m_trickLists) == 0xA28 && offsetof(CTrickComponent, m_queue) == 0xA34 &&
                  offsetof(CTrickComponent, m_extraListCount) == 0x1204 &&
                  offsetof(CTrickComponent, m_manualList) == 0x1230,
              "layout must match the original");

// 0x0051EA40 (the button in EAX; ECX and EDX kept): whether `button` is one of the d-pad's eight directions (Up, Down,
// Left, Right and the diagonals).
bool IsDirection(uint32_t button);
void IsDirectionStub();
// 0x0051EBD0's entry: MarkUsed keeping ECX and EDX (its callers use ECX after it).
void MarkUsedStub();

} // namespace Obj
