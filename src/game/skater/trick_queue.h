#pragma once

#include <cstdint>

#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Obj {

struct CSkaterProfile;

// The skater's Trick component (type trick 0x270F56E1, vtable 0x0064C508, 0x1BC0 bytes; made by 0x00523490): the
// buttons pressed and released (a buffer of the last 100 events), the tricks their triggers start - the queued
// tricks (DoNextTrick), the manual tricks, the extra grind tricks and the extra tricks a script asks for, each a
// global array of trick structures (Scr / Params, Trigger / Alt_Trigger, a trickSlot the profile's trick mapping
// can fill) - button mashing, and the trick display (the trick name, its score, the spin). Its constructor, its
// Finalize and the button / trigger tests are at 0x0051EA90-0x00521B8F; the pieces here are 0x00521BA0-0x00524411
// (trick_queue.cpp). Its layout is only partly known: fields are reached by offset (named in trick_queue.cpp).
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 3 Finalize (0x0051F2B0), 12 CallMemberFunction.
class CTrickComponent : public CBaseComponent {
public:
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

    // --- member functions reached through their records (the table at 0x00683148) ---
    // 0x00522280 (Pressed): whether the unnamed button was pressed within dur= (15) ms (0x0051F6B0).
    int Pressed(Script::CStruct* params, Script::CScript* script);
    // 0x005222F0 (GetCurrentBailEndpose): current_bail_endpose (the end pose, or none) added to the script's
    // params. True.
    int GetCurrentBailEndpose(Script::CStruct* params, Script::CScript* script);
};

// 0x00523350 (the C function DoNextTrick): the trick component's DoNextTrick with ScriptToRunFirst, Params and a
// copy of TrickParams. True.
bool __cdecl CFunc_DoNextTrick(Script::CStruct* params, Script::CScript* script);

} // namespace Obj
