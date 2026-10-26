#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Obj {

struct CSkaterProfile;

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

// The skater's Trick component (type trick 0x270F56E1, vtable 0x0064C508, 0x1BC0 bytes; made by 0x00523490): the
// buttons pressed and released, the tricks their triggers start - the queued tricks (DoNextTrick), the manual tricks,
// the extra grind tricks and the extra tricks a script asks for, each a global array of trick structures (Scr /
// Params, Trigger / Alt_Trigger, a trickSlot the profile's trick mapping can fill) - button mashing, and the trick
// display (the trick name, its score, the spin). Every frame the pad's buttons are turned into events (pressed /
// released, with the time) in a ring of 100; a trick's trigger (Press, Tap, Release, InOrder, TripleInOrder,
// AirTrickLogic...) is matched against the newest events within its time, and the events it used are marked so they
// aren't used twice.
//
// Two ranges: 0x0051EA40-0x00521B90 (trick_component.cpp: the event ring, its checks and the trick lists'
// queue) and 0x00521BA0-0x00524411 (trick_queue.cpp, trick_member_functions.cpp: the constructor, Update, the tricks,
// mashing, the member functions). Only the fields the first part uses are named below; the second reaches its fields
// by offset (named in trick_queue.cpp).
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 3 Finalize (0x0051F2B0), 12 CallMemberFunction.
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

    // --- 0x00521BA0-0x00524411 (trick_queue.cpp, trick_member_functions.cpp) ---
    // 0x00523490: a new one (0 if out of memory): 0x1BC0 bytes, constructed.
    static CBaseComponent* __cdecl Create();
    // 0x00522390: the constructor: the base's, its vtable and type, its trick object list (+0x13B4), no trick
    // mappings, no components, reset (0x0051F320). Returns this.
    CTrickComponent* Construct();
    // 0x005234F0: the destructor body: its trick mappings structure freed, then the base's. 0x005243F0 (slot 0):
    // the scalar deleting destructor.
    void DestroyBody();
    CTrickComponent* Destroy(unsigned int flags);
    // 0x005231D0 (slot 1): one frame (suspended - vtable +0x1C - while its object is: the object's +0xBC clear):
    // the buttons, the balance trick's buttons, the extra tricks, 0x00521A10, the manual tricks (the pending one
    // dropped after its time), the extra grind tricks (likewise), button mashing.
    void Update();

    // 0x00522410: the trick mapping structure (made if needed) filled from `profile`: its 12 special trick slots
    // (keys = trick, an integer when the slot's flag is set, else a name), then its trick_mapping info.
    void SetTrickMappings(CSkaterProfile* profile);
    // 0x00521E50: the 21 buttons' states from the input's mask (+0x1C): the d-pad's direction (1-8) then the
    // others (0x0051F8F0 each).
    void UpdateButtons();

    // --- the tricks ---
    // 0x00522520: `trick` started if it has a script (Scr; a Grind needs a rail: the core physics' +0x22C's
    // +0x4C): the pending manual and extra grind tricks dropped, its script switched to with its Params, `flag`
    // added to the script's params (unnamed) and `extraParams` appended, updated once; the state component told,
    // the trick counted (0x00522340). A trick with Params without IsExtra is counted by the 0x4FF8DAC6 component
    // first. Whether it started.
    bool TryTrick(Script::CStruct* trick, uint32_t flag, Script::CStruct* extraParams);
    // 0x00522670: the next trick of the queue (skipping empty entries): `first` run first as an interrupt (with
    // `params`); the trick tried; failing that its trickSlot's mapping: an integer starts CreateATrick (with
    // trick_index and `trickParams`), a name the structure it names (the goal manager told first).
    void DoNextTrick(uint32_t first, Script::CStruct* params, Script::CStruct* trickParams);
    // 0x005228B0: the pending manual trick (DoNextManualTrick): ScriptToRunFirst run first as an interrupt with
    // Params (on a copy of `params`, copied back); its slot's mapping; its script switched to with `params` (its
    // Params appended) or its Params.
    void DoNextManualTrick(Script::CStruct* params);
    // 0x00522E80: the pending extra grind trick: its Scripts array (or Template + prefix, by name) picked by the four
    // flags (bits 1, 2, 4, 8), switched to with its Params; the core physics' +0x3A8 cleared for a skater. Whether
    // there was one.
    bool DoExtraGrindTrick(bool a, bool b, bool c, bool d);
    // 0x00521A80 (not here) checks one manual trick array; 0x00521BA0: the special one (with the flag +0x1C's
    // +0x74), then - unless that set one pending - the others.
    void UpdateManualTricks();
    // 0x00521C10: the tricks of `array` whose triggers (0x0051FDA0 with 8) are done: each becomes the pending
    // extra grind trick (with its duration). 0x00521D20: the special array (`special` set) then the others.
    void CheckExtraGrindTricks(uint32_t array, bool special);
    void UpdateExtraGrindTricks();
    // 0x00522AA0: the tricks of `array` not in `skip` (a bit per trick) whose triggers (0x0051FDA0 with 2) are done
    // tried (or their slot's mapping); stops once the extra tricks are killed. Whether one started.
    bool TryExtraTricks(uint32_t array, uint32_t skip);
    // 0x00522C00: while extra tricks are on (until their duration, unless forever): the special array (the flag
    // +0x1C's +0x74; m_isSpecial while it's tried), then the others.
    void UpdateExtraTricks();
    // 0x00522CD0 (SetExtraTricks): on, for duration= frames (16.67 ms each; none: forever); the arrays (tricks= a
    // name, or the unnamed names), special=, the masks of the tricks to skip from ignore= (a string - its address
    // used as a name, as the original does - or an array of names).
    void SetExtraTricks(Script::CStruct* params, Script::CScript* script);
    // 0x00521D80: the bits of `array`'s tricks that 0x0051EF50 matches `ignore` with (the array must exist).
    // 0x00521DF0: those bits added to each extra trick array's mask (and the special one's).
    uint32_t GetIgnoreMask(uint32_t array, uint32_t ignore);
    void IgnoreTricks(uint32_t ignore);

    // --- the button buffer ---
    // 0x00522030: button mashing's speed-up: the presses / releases (not triangle) within BashPeriod marked used,
    // x BashSpeedupFactor, at most BashMaxPercentSpeedup %, plus 1 (unrounded). 0x00522170: the triangle ones
    // counted (and marked).
    double GetBashSpeed();
    double CountTriangleBashes();
    // 0x005230E0: while mashing (BashOn): in the bail's RunOutQuick (or 0xA4B7A992) animation, its speed; else,
    // mashed fast enough, 2 in the state's +0x1C case, or TantrumOff and (once, with a bail end pose)
    // bail_quick_getup.
    void UpdateBash();

    // --- the trick object list (+0x13B4) ---
    // 0x00522340: a trick done: past 3, the object's 0x94182A08 event. 0x005232E0: the count, the two flags and the
    // list cleared. 0x005232A0: the list on (passed the core physics' +0x1B0 - 0x00588A20) or off (cleared).
    // 0x00522370: `value` passed to the list (0x00588A20) while on. 0x00523310: 0x00588AA0's result, the list off
    // and cleared.
    void CountTrick();
    void ResetTrickObjects();
    void SetTrickObjects(bool on);
    void PassToTrickObjects(uint32_t value);
    uint32_t EndTrickObjects(uint32_t a, uint32_t b);

    // 0x00523560 (slot 12; trick_member_functions.cpp): its script member functions - Released / Held (a button,
    // or buttons=), GetSpazzFactor, GetSpin / GetActualSpin / ResetSpin, the trick arrays (SetQueueTricks,
    // SetManualTricks, SetExtraGrindTricks, SetExtraTricks, KillExtraTricks, ClearTricksFrom), the queue
    // (ClearTrickQueue, DoNextManualTrick, ClearManualTrick, ClearExtraGrindTrick), the trick mappings, the event
    // buffer (ClearEventBuffer, RestoreEvents, UseGrindEvents, 0x070E934D), mashing (BashOn / BashOff, the quick
    // getup), the trick display (SetTrickName, SetTrickScore, display), ClearPanel_Landed / ClearPanel_Bailed.
    // 1 / 0 for true / false, 2 for a name it doesn't know.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);

    // --- member functions reached through their records (the table at 0x00683148) ---
    // 0x00522280 (Pressed): whether the unnamed button was pressed within dur= (15) ms (0x0051F6B0).
    int Pressed(Script::CStruct* params, Script::CScript* script);
    // 0x005222F0 (GetCurrentBailEndpose): current_bail_endpose (the end pose, or none) added to the script's
    // params. True.
    int GetCurrentBailEndpose(Script::CStruct* params, Script::CScript* script);

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

// 0x00523350 (the C function DoNextTrick): the trick component's DoNextTrick with ScriptToRunFirst, Params and a
// copy of TrickParams. True.
bool __cdecl CFunc_DoNextTrick(Script::CStruct* params, Script::CScript* script);

} // namespace Obj
