// The skater's gap component (GameObjects::CRiderGapTracker): see gap_entry.h.

#include "game/rider/gap_entry.h"

#include <cstdint>
#include <cstdio>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/text_store.h"
#include "engine/maths/vec4.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/movie_manager.h"
#include "engine/objects/tracker.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "game/skate/career.h"
#include "game/skate/gap_checklist.h"
#include "game/rider/rider.h"
#include "game/rider/trick_lists.h"
#include "engine/core/stubs.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module.h"
#include "game/park_editing/park_editor.h"
#include "engine/core/code_address.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"
#include "game/rider/gap_entry.h"

namespace GameObjects {

namespace {

extern void** const kVtable;
extern void** const kGapHeadVtable;      // ListKit::Foremost<TrickListEntry>
extern void** const kGapTrickHeadVtable; // ListKit::Foremost<TrickNameEntry>
constexpr uint32_t kSkaterGap = 0x11B4C67F;
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29, kSkaterBalanceTrick = 0x589E5FB3,
                   kSkaterPhysicsControl = 0xD9151D4D, kWalk = 0x726E85AA, kVehicle = 0xE47F1B79, kTrick = 0x270F56E1;
constexpr uint32_t kPlayerVehicle = 0x824C6A24;

// Script member functions
constexpr uint32_t kStartGap = 0x65B9EC39, kEndGap = 0xE5399FB2, kStartGapTrick = 0xFC4F2009,
                   kCheckGapTricks = 0x42F80E5E, kClearGapTricks = 0x0196772B, kDuplicateTrigger = 0x4562E4A6;
// Parameters
constexpr uint32_t kGapID = 0x3B442E26, kTrickText = 0x3EAFA520, kTrickscript = 0xA26994E6, kKeycombo = 0x95E16467,
                   kSpin = 0xEDF5DB70, kPerfect = 0x1C39F1B9, kNumTaps = 0xA4BEE6A1, kTricks = 0x1E26FD3E,
                   kFlags = 0xF4FABE45, kCombinedFlags = 0x2760DE9E, kCarGap = 0xC442E33D,
                   kNotOnlyCar = 0xE3ED4ADE /* (name unknown) */, kNeedsCleanLanding = 0x687E2C25, kText = 0xC4745838,
                   kScore = 0xCD66C8AE, kGapScript = 0x7C9E51AB, kPermanent = 0x23627FD7, kOnlineFlag = 0x20209C31,
                   kContinue = 0xEC1CD520, kNotInSameFrame = 0x97975226, kTriggerScript = 0x2CA8A299;
// Scripts and settings
constexpr uint32_t kDoPerfectCheck2 = 0x35B75889, kDefaultGapScript = 0x541A2485,
                   kProcessAllGapsReward = 0x927DAA70, kOff = 0xD443A2BC;

// Gap flags (gap_entry.h)
constexpr uint32_t kGapGround = 0x1, kGapAir = 0x2, kGapRail = 0x4, kGapWall = 0x8, kGapLip = 0x10,
                   kGapWallplant = 0x20, kGapManual = 0x40, kGapWalk2 = 0x80, kGapWalk3 = 0x100, kGapSkating = 0x200,
                   kGapWalking = 0x400, kGapDriving = 0x800, kGapRequired = 0xFFFF0000;
constexpr int kRequireShift = 16;

template <typename T>
T& Prop(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// A gap in progress (the 0x64-byte TrickListEntry) and a gap trick (the 0x34-byte TrickNameEntry), by their fields.
uint32_t& GapId(TrickListEntry* gap) { return Prop<uint32_t>(gap, 0x14); }
uint32_t& GapOptionLabels(TrickListEntry* gap) { return Prop<uint32_t>(gap, 0x18); }
int& GapNode(TrickListEntry* gap) { return Prop<int>(gap, 0x1C); }
uint32_t& GapKeyCombo(TrickListEntry* gap) { return Prop<uint32_t>(gap, 0x20); }
int& GapTrickCount(TrickListEntry* gap) { return Prop<int>(gap, 0x24); } // the score's count of the trick at the start
uint32_t& GapTrickScript(TrickListEntry* gap) { return Prop<uint32_t>(gap, 0x28); }
uint32_t& GapTrickText(TrickListEntry* gap) { return Prop<uint32_t>(gap, 0x2C); } // (its checksum)
int& GapSpin(TrickListEntry* gap) { return Prop<int>(gap, 0x30); }               // in half turns
uint8_t& GapPerfect(TrickListEntry* gap) { return Prop<uint8_t>(gap, 0x34); }
uint32_t& GapTricksCount(TrickListEntry* gap) { return Prop<uint32_t>(gap, 0x38); } // the score's +0x5C then
int& GapTaps(TrickListEntry* gap) { return Prop<int>(gap, 0x3C); }
Maths::Vec4& GapStartPos(TrickListEntry* gap) { return Prop<Maths::Vec4>(gap, 0x40); }
Maths::Vec4& GapStartDir(TrickListEntry* gap) { return Prop<Maths::Vec4>(gap, 0x50); }

uint32_t& TrickGapId(TrickNameEntry* trick) { return Prop<uint32_t>(trick, 0x20); }
int& TrickNode(TrickNameEntry* trick) { return Prop<int>(trick, 0x24); }
uint32_t& TrickScript(TrickNameEntry* trick) { return Prop<uint32_t>(trick, 0x28); }
uint8_t& TrickDone(TrickNameEntry* trick) { return Prop<uint8_t>(trick, 0x2C); }
uint32_t& TrickFrame(TrickNameEntry* trick) { return Prop<uint32_t>(trick, 0x30); }

// A list's first entry and the one after `node` (null at the head: its data is -1).
template <typename T>
T* EntryOf(MemSys::ListNode* node)
{
    return reinterpret_cast<intptr_t>(node->data) == -1 ? nullptr : reinterpret_cast<T*>(node);
}
template <typename T>
T* First(MemSys::ListNode& head)
{
    return EntryOf<T>(head.next);
}
template <typename T>
T* Next(T* entry)
{
    return EntryOf<T>(reinterpret_cast<MemSys::ListNode*>(entry)->next);
}
// An entry deleted through its vtable (the scalar deleting destructor).
void EraseOne(void* entry)
{
    auto* node = static_cast<MemSys::ListNode*>(entry);
    reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(node->vtable[0])(node, 1);
}
// `node` at the front of the list `head`.
void AddToFront(MemSys::ListNode& head, MemSys::ListNode* node)
{
    node->prev = &head;
    node->next = head.next;
    head.next->prev = node;
    head.next = node;
}
void SetupListSentinel(MemSys::ListNode& head, void** vtable)
{
    head.vtable = vtable;
    head.next = &head;
    head.prev = &head;
    head.priority = 0;
    head.data = reinterpret_cast<void*>(-1);
}

// The skate module (0x007CE478): the current level's gap checklist (0x005656C0) and the career (+0x20). Not
// rewritten yet.
GoalSystem::CGapTally* GapTallyRef()
{
    return static_cast<GoalSystem::CGapTally*>(GameModules::Skate::Sole()->FetchGapList());
}
CRiderStoryProgress* Career() { return *reinterpret_cast<CRiderStoryProgress**>(reinterpret_cast<uint8_t*>(GameModules::g_skateModule) + 0x20); }

// The score (GameModules::PointTally; not rewritten yet): how often a trick has been done - by key combo (0x00562620 /
// 0x00562510 with perfect) or by name (0x005625A0 / 0x005624C0) - with a spin and taps; 0x0049F4C0 (a shared "0"
// for the tricks= arrays); a gap's points and text (0x00563990).
int KeyComboCount(uint8_t* score, uint32_t keyCombo, int spin, int taps)
{
    return reinterpret_cast<GameModules::PointTally*>(score)->CountKeyComboRepeats(keyCombo, spin, taps);
}
int TrickCount(uint8_t* score, uint32_t trick, int spin, int taps)
{
    return reinterpret_cast<GameModules::PointTally*>(score)->CountTrickRepeats(trick, spin, taps);
}
int KeyComboCount(uint8_t* score, uint32_t keyCombo, int spin, uint32_t perfect, int taps)
{
    return reinterpret_cast<GameModules::PointTally*>(score)->CountKeyCombo(keyCombo, spin, reinterpret_cast<bool&>(perfect), taps);
}
int TrickCount(uint8_t* score, uint32_t trick, int spin, uint32_t perfect, int taps)
{
    return reinterpret_cast<GameModules::PointTally*>(score)->CountTrickAfterTaps(trick, spin, reinterpret_cast<bool&>(perfect), taps);
}
int TricksArrayCount(uint8_t* /*score*/, Scripts::CArray* tricks, uint32_t since)
{
    // (the shared "0": a stdcall stub of two stack arguments; the score is only in ECX)
    return Stubs::ReturnZero2(static_cast<int>(reinterpret_cast<intptr_t>(tricks)), static_cast<int>(since));
}
void AddGapScore(uint8_t* score, const char* text, int points, int flags)
{
    reinterpret_cast<GameModules::PointTally*>(score)->FireOff(text, points, static_cast<uint32_t>(flags));
}

// The state component (the physics control's +0x2C): +0x118 driving, +0x1C walking.
uint8_t* StateOf(uint8_t* physicsControl) { return Prop<uint8_t*>(physicsControl, 0x2C); }
// The core physics' state component (+0x1528).
uint8_t* CoreState(uint8_t* corePhysics) { return Prop<uint8_t*>(corePhysics, 0x1528); }

// Spin degrees in half turns (truncated, as the original's division).
int HalfTurns(int degrees) { return degrees / 180; }

} // namespace

// 0x00512C60
CObjectModule* __cdecl CRiderGapTracker::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CRiderGapTracker));
    return memory ? static_cast<CRiderGapTracker*>(memory)->MakeNew() : nullptr;
}

// 0x005122A0
CRiderGapTracker* CRiderGapTracker::MakeNew()
{
    CObjectModule::MakeNew();
    m_vtable = kVtable;
    SetupListSentinel(m_gaps, kGapHeadVtable);
    SetupListSentinel(m_gapMoveChain, kGapTrickHeadVtable);
    m_kindTag = kSkaterGap;
    m_corePhysics = nullptr;
    m_balanceTrick = nullptr;
    return this;
}

// 0x00511F10
void CRiderGapTracker::DestroyBody()
{
    m_vtable = kVtable;
    m_gapMoveChain.UnlinkAs(g_gapTrickNodeVtable);
    m_gaps.UnlinkAs(g_gapNodeVtable);
    CObjectModule::DestroyBody();
}

// 0x005122F0
CRiderGapTracker* CRiderGapTracker::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00511BB0
void CRiderGapTracker::ConfigureFromData(Scripts::CStruct* /*params*/) { m_tick = 0; }

// 0x00511BC0
void CRiderGapTracker::Seal()
{
    m_corePhysics = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kSkaterCorePhysics));
    m_balanceTrick = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kSkaterBalanceTrick));
    m_physicsControl = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kSkaterPhysicsControl));
    m_walk = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kWalk));
}

// 0x00512320
void CRiderGapTracker::TickFrame()
{
    ++m_tick;
    uint32_t cancel, require;
    FetchGapConditionMasks(&cancel, &require);
    ScanForGaps(cancel, require);
}

// 0x00511C80
void CRiderGapTracker::FetchGapConditionMasks(uint32_t* cancel, uint32_t* require)
{
    *require = 0;
    *cancel = 0;
    // (each state bit goes into both: `cancel` as it is, `require` << 16)
    auto set = [cancel, require](uint32_t bit) {
        *cancel |= bit;
        *require |= bit << kRequireShift;
    };
    uint8_t* state = StateOf(m_physicsControl);
    if (Prop<uint8_t>(state, 0x118)) {
        // Driving: on the ground while the vehicle (PlayerVehicle) has wheels down (+0x114), else in the air.
        *cancel = kGapDriving;
        *require |= kGapDriving << kRequireShift;
        CGameEntity* vehicle =
            static_cast<CGameEntity*>(ObjectHubSlot()->LookUpEntity(kPlayerVehicle));
        uint8_t* physics = reinterpret_cast<uint8_t*>(vehicle->FetchModule(kVehicle));
        set(Prop<uint32_t>(physics, 0x114) != 0 ? kGapGround : kGapAir);
        return;
    }
    if (Prop<int>(state, 0x1C) != 0) {
        // Walking: the walk component's state.
        *cancel = kGapWalking;
        *require |= kGapWalking << kRequireShift;
        switch (Prop<int>(m_walk, 0x18)) {
        case 0: set(kGapGround); break;
        case 1: set(kGapAir); break;
        case 2: set(kGapWalk2); break;
        case 3: set(kGapWalk3); break;
        default: break;
        }
        return;
    }
    // Skating: the core physics' state.
    *cancel = kGapSkating;
    *require |= kGapSkating << kRequireShift;
    uint8_t* coreState = CoreState(m_corePhysics);
    switch (Prop<int>(coreState, 0x18)) {
    case 0: { // the ground: plain (+0x90 and the balance trick's two clear) or a manual
        bool balancing = Prop<uint32_t>(m_balanceTrick, 0x18) != 0 || Prop<uint8_t>(m_balanceTrick, 0x1C) != 0;
        if (!Prop<uint8_t>(coreState, 0x90) && !balancing)
            set(kGapGround);
        if (balancing)
            set(kGapManual);
        break;
    }
    case 1: set(kGapAir); break;
    case 2: set(kGapWall); break;
    case 3: set(kGapLip); break;
    case 4: set(kGapRail); break;
    case 5: set(kGapWallplant); break;
    default: break;
    }
}

// 0x00511F60
void CRiderGapTracker::ScanForGaps(uint32_t cancel, uint32_t require)
{
    for (TrickListEntry* gap = First<TrickListEntry>(m_gaps); gap;) {
        TrickListEntry* next = Next(gap);
        GapOptionLabels(gap) &= ~require;
        if (GapOptionLabels(gap) & cancel) {
            if (uint32_t script = GapTrickScript(gap)) {
                for (TrickNameEntry* trick = First<TrickNameEntry>(m_gapMoveChain); trick; trick = Next(trick)) {
                    if (!TrickDone(trick) && TrickGapId(trick) == GapId(gap) && TrickScript(trick) == script &&
                        TrickNode(trick) == GapNode(gap)) {
                        EraseOne(trick);
                        break;
                    }
                }
            }
            EraseOne(gap);
        }
        gap = next;
    }
}

// 0x00512030
void CRiderGapTracker::ClearGaps()
{
    reinterpret_cast<ListHead*>(&m_gaps)->DeleteAllNodes();
    reinterpret_cast<ListHead*>(&m_gapMoveChain)->DeleteAllNodes();
}

// 0x00512050
void CRiderGapTracker::StartGapTrick(Scripts::CStruct* params, Scripts::CScript* script)
{
    void* memory = MemSys::AllocZeroed(sizeof(TrickNameEntry));
    auto* trick = memory ? static_cast<TrickNameEntry*>(memory)->MakeNew() : nullptr;
    AddToFront(m_gapMoveChain, &trick->m_listLink);
    TrickNode(trick) = script->m_listLink;
    TrickDone(trick) = 0;
    TrickFrame(trick) = m_tick;
    uint32_t gapId = 0;
    params->ReadHashValue(kGapID, &gapId, 0);
    TrickGapId(trick) = gapId;
    const char* text = nullptr;
    params->FetchText(kTrickText, &text, 0);
    trick->m_label = text;
    TrickScript(trick) = 0;
    params->ReadHashValue(kTrickscript, &TrickScript(trick), 1);
}

// 0x00512140
void CRiderGapTracker::CheckGapTricks(Scripts::CStruct* /*params*/)
{
    for (TrickNameEntry* trick = First<TrickNameEntry>(m_gapMoveChain); trick;) {
        TrickNameEntry* next = Next(trick);
        if (TrickDone(trick))
            m_entity->StartScriptTask(TrickScript(trick), TrickNode(trick), false, false, nullptr);
        trick = next;
    }
}

// 0x005121A0
void CRiderGapTracker::ClearGapTricks(Scripts::CStruct* params)
{
    bool notThisFrame = params->HasBareFlag(kNotInSameFrame);
    for (TrickNameEntry* trick = First<TrickNameEntry>(m_gapMoveChain); trick;) {
        TrickNameEntry* next = Next(trick);
        if (!notThisFrame || TrickFrame(trick) != m_tick)
            EraseOne(trick);
        trick = next;
    }
}

// 0x00512360
void CRiderGapTracker::StartGap(Scripts::CStruct* params, Scripts::CScript* script)
{
    if (Prop<uint8_t>(CoreState(m_corePhysics), 0x70))
        return;
    uint32_t gapId = 0;
    params->ReadHashValue(kGapID, &gapId, 0);
    // A gap of the same GapID and node is started again.
    for (TrickListEntry* gap = First<TrickListEntry>(m_gaps); gap;) {
        TrickListEntry* next = Next(gap);
        if (GapId(gap) == gapId && GapNode(gap) == script->m_listLink)
            EraseOne(gap);
        gap = next;
    }
    void* memory = MemSys::AllocZeroed(sizeof(TrickListEntry));
    auto* gap = memory ? static_cast<TrickListEntry*>(memory)->MakeNew() : nullptr;
    AddToFront(m_gaps, &gap->m_listLink);
    GapId(gap) = gapId;
    GapNode(gap) = script->m_listLink;
    GapTrickText(gap) = 0;
    // Where the skater starts: its camera's position, and facing back along the camera's at row.
    auto* skater = static_cast<CRider*>(m_entity);
    GapStartPos(gap) = static_cast<CGameEntity*>(skater->GetSkaterCamera())->m_atPoint;
    const Maths::Vec4& at = static_cast<CGameEntity*>(skater->GetSkaterCamera())->m_basisFrame.row[2];
    Maths::Vec4& dir = GapStartDir(gap);
    dir.z = -at.z;
    dir.x = -at.x;
    dir.y = -at.y;
    dir.w = -at.w;

    // The trick it needs.
    uint32_t trickScript;
    if (params->ReadHashValue(kTrickscript, &trickScript, 0)) {
        GapTrickScript(gap) = trickScript;
        uint32_t keyCombo;
        const char* text;
        Scripts::CArray* tricks;
        auto readSpin = [&]() {
            int spin;
            if (params->FetchIntValue(kSpin, &spin, 0)) {
                GapPerfect(gap) = params->HasBareFlag(kPerfect) ? 1 : 0;
                GapSpin(gap) = HalfTurns(spin);
            }
            params->FetchIntValue(kNumTaps, &GapTaps(gap), 0);
        };
        if (params->ReadHashValue(kKeycombo, &keyCombo, 0)) {
            GapKeyCombo(gap) = keyCombo;
            readSpin();
            GapTrickCount(gap) = KeyComboCount(m_pointsHeld, keyCombo, GapSpin(gap), GapTaps(gap));
        } else if (params->FetchText(kTrickText, &text, 0)) {
            GapTrickText(gap) = Checksums::HashFromText(text);
            readSpin();
            GapTrickCount(gap) = TrickCount(m_pointsHeld, GapTrickText(gap), GapSpin(gap), GapTaps(gap));
        } else if (params->FetchList(kTricks, &tricks, 0)) {
            Scripts::DuplicateArray(gap->m_elementList, tricks);
            GapTricksCount(gap) = Prop<uint32_t>(m_pointsHeld, 0x5C);
        }
        StartGapTrick(params, script);
    }

    // Its flags.
    GapOptionLabels(gap) = 0;
    Scripts::CArray* flags = nullptr;
    params->FetchList(kFlags, &flags, 0);
    if (flags) {
        for (uint32_t i = 0; i < flags->m_extent; ++i)
            GapOptionLabels(gap) |= Scripts::FetchIntValue(flags->FetchPart(i));
    } else {
        uint32_t flag;
        if (params->ReadHashValue(kFlags, &flag, 0))
            GapOptionLabels(gap) |= Scripts::FetchIntValue(flag);
    }
    uint32_t combined = 0;
    if (params->ReadHashValue(kCombinedFlags, &combined, 0))
        GapOptionLabels(gap) |= combined;
    if (params->HasBareFlag(kCarGap))
        GapOptionLabels(gap) |= kGapSkating | kGapWalking;
    else if (!params->HasBareFlag(kNotOnlyCar) && !(GapOptionLabels(gap) & (kGapSkating | kGapWalking)))
        GapOptionLabels(gap) |= kGapDriving;

    // Checked against the state now - unless gap checks are Off (the settings' +0x40), when driving can't matter.
    bool drivingMatters = Prop<uint8_t>(StateOf(m_physicsControl), 0x118) && (GapOptionLabels(gap) & kGapDriving);
    if (!drivingMatters && Prop<uint32_t>(reinterpret_cast<uint8_t*>(ParkEditing::g_parkEditor), 0x40) == kOff)
        return;
    uint32_t cancel, require;
    FetchGapConditionMasks(&cancel, &require);
    ScanForGaps(cancel, require);
}

// 0x00512770
void CRiderGapTracker::EndGap(Scripts::CStruct* params, Scripts::CScript* script)
{
    if (Prop<uint8_t>(CoreState(m_corePhysics), 0x70))
        return;
    uint32_t gapId = 0;
    params->ReadHashValue(kGapID, &gapId, 0);
    for (TrickListEntry* gap = First<TrickListEntry>(m_gaps); gap;) {
        TrickListEntry* next = Next(gap);
        // Its GapID - without one, a gap started at a node that links to this script's.
        if (GapId(gap) != gapId || (gapId == 0 && !LevelNodes::HasLinkTo(GapNode(gap), script->m_listLink))) {
            gap = next;
            continue;
        }
        // Failed: a required state never seen, or not landed perfectly when that's required.
        if (GapOptionLabels(gap) & kGapRequired) {
            EraseOne(gap);
            gap = next;
            continue;
        }
        if (params->HasEntryNamed(kNeedsCleanLanding)) {
            Scripts::CStruct* results = Scripts::SpawnParamBag();
            if (results)
                results = results->PrepareAll();
            Scripts::RunScriptOnce(kDoPerfectCheck2, nullptr, m_entity, results);
            int perfect = 0; // (not initialised in the original: FetchIntValue asserts it's there)
            results->FetchIntValue(kPerfect, &perfect, 1);
            if (perfect != 1) {
                EraseOne(gap); // (the results structure isn't freed here: the original leaks it)
                gap = next;
                continue;
            }
            if (results) {
                results->Clear();
                Scripts::DeleteStruct(results);
            }
        }
        const char* text;
        if (!params->FetchText(kText, &text, 0))
            text = "Unnamed Gap";
        char coloured[256];
        std::snprintf(coloured, sizeof(coloured), "\\c1%s\\c0", text);

        // Its gap trick done: no trick needed, or the trick was done since the gap started.
        if (GapTrickScript(gap)) {
            bool done;
            if (!GapKeyCombo(gap) && !GapTrickText(gap) && gap->m_elementList->m_extent == 0) {
                done = true;
            } else {
                done = false;
                if (GapKeyCombo(gap) && GapTrickCount(gap) < KeyComboCount(m_pointsHeld, GapKeyCombo(gap), GapSpin(gap),
                                                                           GapPerfect(gap), GapTaps(gap)))
                    done = true;
                else if (GapTrickText(gap) && GapTrickCount(gap) < TrickCount(m_pointsHeld, GapTrickText(gap), GapSpin(gap),
                                                                               GapPerfect(gap), GapTaps(gap)))
                    done = true;
                else if (gap->m_elementList->m_extent > 0 &&
                         TricksArrayCount(m_pointsHeld, gap->m_elementList, GapTricksCount(gap)) > 0)
                    done = true;
            }
            if (done) {
                for (TrickNameEntry* trick = First<TrickNameEntry>(m_gapMoveChain); trick; trick = Next(trick)) {
                    if (TrickGapId(trick) == GapId(gap)) {
                        // (0x00578670, trick_lists.h's TrickListEntry::SetFlag: the flag at +0x2C - here the gap
                        // trick's done)
                        reinterpret_cast<TrickListEntry*>(trick)->SetFlag();
                        break;
                    }
                }
            }
        }
        GapTallyRef()->SetGapPositions(text, GapStartPos(gap), GapStartDir(gap));
        int points = 0;
        if (params->FetchIntValue(kScore, &points, 0) && m_entity->m_unknownBC) {
            AddGapScore(m_pointsHeld, coloured, points, 0x10);
            Prop<uint8_t>(m_entity->FetchModule(kTrick), 0x13A8) = 1;
            GapTallyRef()->RegisterGapHit(text);
            m_entity->StartScriptTask(kDefaultGapScript, 0, false, false, nullptr);
        }
        uint32_t gapScript;
        if (params->ReadHashValue(kGapScript, &gapScript, 0)) {
            bool permanent = params->HasBareFlag(kPermanent);
            bool replicateFlag = params->HasBareFlag(kOnlineFlag);
            m_entity->StartScriptTask(gapScript, script->m_listLink, replicateFlag, permanent, nullptr);
        }
        Scripts::CStruct* continuation = nullptr;
        if (params->ReadSubStruct(kContinue, &continuation, 0))
            StartGap(continuation, script);
        uint32_t success = Checksums::FoldTextIntoChecksum(gapId, "_Success");
        Tracker()->LaunchEvent(success, 0xFFFFFFFF, m_entity->m_ident, params, true, -1.0f);
        // The gap deleted if it's still in the list; then the walk begins again.
        for (TrickListEntry* other = First<TrickListEntry>(m_gaps); other; other = Next(other)) {
            if (other == gap) {
                EraseOne(gap);
                break;
            }
        }
        gap = First<TrickListEntry>(m_gaps);
    }
}

// 0x00512CC0
int CRiderGapTracker::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script)
{
    switch (name) {
    case kDuplicateTrigger: {
        int node = script->m_listLink;
        if (!node)
            return 1;
        int links = static_cast<int>(LevelNodes::GetLinkCount(static_cast<uint32_t>(node)));
        for (int i = 0; i < links; ++i) {
            Scripts::CStruct* linked = LevelNodes::FetchNeighbourNode(static_cast<uint32_t>(node), i);
            uint32_t triggerScript = 0;
            if (linked->ReadHashValue(kTriggerScript, &triggerScript, 0)) {
                m_entity->StartScriptTask(triggerScript, script->m_listLink, false, false, nullptr);
                return 1;
            }
        }
        return 1;
    }
    case kCheckGapTricks: CheckGapTricks(params); return 1;
    case kClearGapTricks: ClearGapTricks(params); return 1;
    case kStartGap: StartGap(params, script); return 1;
    case kStartGapTrick: StartGapTrick(params, script); return 1;
    case kEndGap: EndGap(params, script); return 1;
    default: return 2;
    }
}

// 0x00511E50
void GapListNode::DestroyBody() { UnlinkAs(g_gapNodeVtable); }
// 0x00511E90
GapListNode* GapListNode::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}
// 0x00512240
GapListNode* GapListNode::DestroyHead(unsigned int flags) { return Teardown(flags); }

// 0x00511E70
void GapTrickListNode::DestroyBody() { UnlinkAs(g_gapTrickNodeVtable); }
// 0x00511ED0
GapTrickListNode* GapTrickListNode::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}
// 0x00512270
GapTrickListNode* GapTrickListNode::DestroyHead(unsigned int flags) { return Teardown(flags); }

// 0x00511C10
void __cdecl DiscardQueuedGaps() { GapTallyRef()->ClearPending(); }

// 0x00511C30
void __cdecl PayOutQueuedGaps()
{
    GapTallyRef()->PayOutQueuedGaps();
    if (Career()->AllGapsGot())
        Scripts::SpawnScript(kProcessAllGapsReward, nullptr, 0, nullptr, 0, 0, false, false, false, false);
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CRiderGapTracker::Teardown,
         &GameObjects::CRiderGapTracker::TickFrame,
         &GameObjects::CRiderGapTracker::ConfigureFromData,
         &GameObjects::CRiderGapTracker::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::CRiderGapTracker::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);
static VtableSlot kGapHeadVtable_slots[] = {&GameObjects::GapListNode::DestroyHead};
void** const kGapHeadVtable = reinterpret_cast<void**>(kGapHeadVtable_slots);
static VtableSlot kGapTrickHeadVtable_slots[] = {&GameObjects::GapTrickListNode::DestroyHead};
void** const kGapTrickHeadVtable = reinterpret_cast<void**>(kGapTrickHeadVtable_slots);

} // namespace
} // namespace Obj

namespace GameObjects {

static VtableSlot g_gapNodeVtable_slots[] = {&GameObjects::GapListNode::Teardown};
void** const g_gapNodeVtable = reinterpret_cast<void**>(g_gapNodeVtable_slots);

} // namespace Obj

namespace GameObjects {

static VtableSlot g_gapTrickNodeVtable_slots[] = {&GameObjects::GapTrickListNode::Teardown};
void** const g_gapTrickNodeVtable = reinterpret_cast<void**>(g_gapTrickNodeVtable_slots);

} // namespace Obj
