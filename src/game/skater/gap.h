#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

// The skater's gap component (SkaterGap, type 0x11B4C67F, vtable 0x0064BE90, 0x58 bytes): the gaps the scripts start
// (StartGap) and end (EndGap) - each a 0x64-byte entry in the first list (trick_lists.h's TrickListEntry: GapID, its
// node, the cancel / require flags, the trick it may need, where the skater started) - and the gap tricks
// (StartGapTrick: trick_lists.h's TrickNameEntry - TrickText, GapID, its trickscript, done) that run when their gap is
// landed with the trick (CheckGapTricks). Each frame the skater's physics state cancels the gaps that don't allow it
// and ticks off what they require. From the disassembly of 0x00511BB0-0x00512E21 (0x00511C10, 0x00511C30, 0x00512140
// and 0x005121A0 aren't Ghidra functions).
//
// Gap flags (an entry's +0x18): the low half cancels the gap when the skater is in that state - 0x1 ground (or a
// wheeled vehicle on the ground / walking on the ground), 0x2 air, 0x4 rail, 0x8 wall, 0x10 lip, 0x20 wallplant, 0x40
// manual, 0x80 / 0x100 walking states 2 / 3, 0x200 skating, 0x400 walking, 0x800 driving; the high half (the same
// bits << 16) are states the gap requires: cleared when the state is seen, and a gap ending with any left fails.
//
// Vtable: 0 Destroy, 1 Update, 2 InitFromStructure, 3 Finalize, 12 CallMemberFunction.
namespace Obj {

struct TrickListEntry;
struct TrickNameEntry;

class CSkaterGapComponent : public CBaseComponent {
public:
    // 0x00512C60 (registered by RegisterComponentTypes): a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x005122A0: the base, its vtable and type, two empty lists, no core physics or balance trick yet. Returns this.
    CSkaterGapComponent* Construct();
    // 0x00511F10: the two lists' heads unlinked (their entries aren't freed: ClearGaps), then the base's.
    void DestroyBody();
    // 0x005122F0 (vtable slot 0)
    CSkaterGapComponent* Destroy(unsigned int flags);
    // 0x00511BB0 (vtable +0x08): the frame count reset. `params` isn't read.
    void InitFromStructure(Script::CStruct* params);
    // 0x00511BC0 (vtable +0x0C): the core physics, balance trick, physics control and walk components found.
    void Finalize();
    // 0x00512320 (vtable +0x04): one more frame; the gaps checked against the skater's state.
    void Update();
    // 0x00512CC0 (vtable +0x30): StartGap, EndGap, StartGapTrick, CheckGapTricks, ClearGapTricks, DuplicateTrigger (the
    // TriggerScript of the first node the script's node links to that has one, run on the skater) - 1; 2 for any
    // other name.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);

    // 0x00511C80: the flags of the skater's state now: `cancel` the low ones (one state bit, and 0x200 skating / 0x400
    // walking / 0x800 driving), `require` the same << 16.
    void GetStateFlags(uint32_t* cancel, uint32_t* require);
    // 0x00511F60: each gap's required states seen now cleared; the gaps the state cancels deleted, with their gap
    // trick (the first one of the same GapID, node and trickscript that isn't done).
    void CheckGaps(uint32_t cancel, uint32_t require);
    // 0x00512030: every gap and gap trick deleted.
    void ClearGaps();
    // 0x00512050 (StartGapTrick): a gap trick added at the front: GapID, TrickText, trickscript, the script's node and
    // this frame.
    void StartGapTrick(Script::CStruct* params, Script::CScript* script);
    // 0x00512140 (CheckGapTricks): each done gap trick's trickscript run on the skater (at its node). `params` isn't
    // read.
    void CheckGapTricks(Script::CStruct* params);
    // 0x005121A0 (ClearGapTricks): the gap tricks deleted - with NotInSameFrame only those not added this frame.
    void ClearGapTricks(Script::CStruct* params);
    // 0x00512360 (StartGap): unless the skater's state says not to (+0x70), a gap (any of the same GapID and node
    // replaced) at the front: where the skater (its camera) is and faces; the trick it needs (trickscript with a
    // Keycombo, a TrickText or tricks; spin, Perfect, num_taps; the score's count of that trick now) - also a gap
    // trick then; the flags (flags - one or an array of script integers -, combined_flags, CarGap: only driving;
    // otherwise not driving unless 0xE3ED4ADE). Then checked against the state at once (unless gap checks are Off
    // and nothing about driving applies).
    void StartGap(Script::CStruct* params, Script::CScript* script);
    // 0x00512770 (EndGap): each gap of the GapID (none: one started at a node linked to the script's) ends: failed
    // (deleted) with requirements left, or without a perfect landing when Require_Perfect; else its gap trick is
    // done if the trick was done since (or none was needed), the checklist gets the skater's start for the text,
    // a local skater with a score gets it (and the checklist the gap, DefaultGapScript run), the GapScript is run
    // (NetEnabled, permanent), Continue starts a new gap, <GapID>_Success is sent, and the gap deleted. The walk
    // begins again after each.
    void EndGap(Script::CStruct* params, Script::CScript* script);

    Mem::ListNode m_gaps;      // +0x18 Lst::Head<TrickListEntry> (vtable 0x0064BEC4)
    Mem::ListNode m_gapTricks; // +0x2C Lst::Head<TrickNameEntry> (vtable 0x0064BEC8)
    uint32_t m_frame;          // +0x40 counted by Update
    uint8_t* m_score;          // +0x44 the skater's score (Mdl::Score; set by the skater)
    uint8_t* m_corePhysics;    // +0x48 SkaterCorePhysics (0x5BD63B29)
    uint8_t* m_balanceTrick;   // +0x4C SkaterBalanceTrick (0x589E5FB3)
    uint8_t* m_physicsControl; // +0x50 SkaterPhysicsControl (0xD9151D4D)
    uint8_t* m_walk;           // +0x54 Walk (0x726E85AA)
};
static_assert(offsetof(CSkaterGapComponent, m_gaps) == 0x18 && offsetof(CSkaterGapComponent, m_gapTricks) == 0x2C &&
                  offsetof(CSkaterGapComponent, m_frame) == 0x40 && offsetof(CSkaterGapComponent, m_score) == 0x44 &&
                  offsetof(CSkaterGapComponent, m_corePhysics) == 0x48 &&
                  offsetof(CSkaterGapComponent, m_walk) == 0x54 && sizeof(CSkaterGapComponent) == 0x58,
              "layout must match the original (0x00512C60 allocates 0x58 bytes)");

// The two lists' node types (Lst::Node<TrickListEntry>, vtable 0x0064BE88; Lst::Node<TrickNameEntry>, 0x0064BE8C) and
// heads (Lst::Head<...>, 0x0064BEC4 / 0x0064BEC8): the destructor bodies unlink.
struct GapListNode : Mem::ListNode {
    // 0x00511E50 / 0x00511E90: Lst::Node<TrickListEntry>'s destructor body and scalar deleting destructor.
    void DestroyBody();
    GapListNode* Destroy(unsigned int flags);
    // 0x00512240: Lst::Head<TrickListEntry>'s scalar deleting destructor.
    GapListNode* DestroyHead(unsigned int flags);
};
struct GapTrickListNode : Mem::ListNode {
    // 0x00511E70 / 0x00511ED0 / 0x00512270: the same for TrickNameEntry.
    void DestroyBody();
    GapTrickListNode* Destroy(unsigned int flags);
    GapTrickListNode* DestroyHead(unsigned int flags);
};

// The current level's gap checklist (Mdl::Skate 0x005656C0) - called from the trick component:
// 0x00511C10: the pending landings dropped (ClearPending).
void __cdecl ClearPendingGaps();
// 0x00511C30: the pending landings counted (AwardPendingGaps); once every level's gaps are got (the career's
// AllGapsGot), ProcessAllGapsReward is spawned.
void __cdecl AwardPendingGaps();

// 0x0064BE88: gapNodeVtable (defined where the class's functions are).
extern void** const g_gapNodeVtable;

// 0x0064BE8C: gapTrickNodeVtable (defined where the class's functions are).
extern void** const g_gapTrickNodeVtable;

} // namespace Obj
