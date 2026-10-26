#pragma once

// The animation blender component (type 0x7149B753, 0x44 bytes, vtable 0x00645D8C): an object's blend channels
// (Gfx::CBlendChannel, 0x74 bytes each, on a list) - most of its methods pass on to the first of them - and the
// observers told of each blended pose. (Our name for the class; other code calls it "the animation blender".)

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "game/mth/vector.h"
#include "game/obj/object.h"

namespace Gfx {
class CBlendChannel;
struct CPose;
} // namespace Gfx

namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Obj {

class CAnimBlenderComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x7149B753;

    // 0x0041A5B0: a new one (zeroed memory, Construct). 0x00419690 (not a Ghidra function; registered with Create):
    // its script member functions (the table at 0x00682350, 0x18 bytes a record) registered.
    static CBaseComponent* __cdecl Create();
    static void __cdecl RegisterMemberFunctions();
    // 0x0041A410: the base's constructor, its vtable, an empty channel list, an empty observer list (its
    // sentinel node allocated), its type. Returns this.
    CAnimBlenderComponent* Construct();
    // 0x0041A4C0: the channels deleted, each observer deleted and the list emptied (twice) and its sentinel
    // freed, the channel list's head unlinked, the base's destructor body.
    void DestroyBody();
    // 0x0041A610 (vtable slot 0)
    CAnimBlenderComponent* Destroy(unsigned int flags);

    // --- the vtable ---
    // 0x0041A260 (slot 1): each channel updated (those after the first with a weight below 0.00001 only reset,
    // weight 0); if any changed the pose, the poses blended into the shared buffer 0x006F8A60 bone set by bone
    // set (weights 0x006F8A5C, sets 0x006F8A58), the observers told and the skeleton given it.
    void Update();
    // 0x00419DD0 (slot 2): with no channel yet, one made (at the front) and set up from `params` on the
    // bottom-up heap.
    void InitFromStructure(Script::CStruct* params);
    // 0x004196D0 (slot 3): each channel's slot 2, then the object's 0x222756D5 (skeleton), 0x286A8D26 (model) and
    // 0xCE0CA665 components kept.
    void Finalize();
    // 0x00419980 (slot 6): `value` to slot 6 of each channel whose id is `id`.
    void PassToChannels(uint32_t value, uint32_t id);
    // 0x00419950 (slot 12): 0x0B1E7291 and 0xEA6D0EFD go to PassToChannel; anything else isn't handled (2).
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);

    // --- script member functions (the table at 0x00682350; each is given its name) ---
    // 0x0041A1A0 [0xCABB44AE]: a channel added (id from 0x5CFCCF35, at the front with flag 0x580C0963; the first
    // channel's anim set added to the parameters as 0x52A864B0), set up from the parameters and its slot 2. 1.
    int AddChannelFromScript(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x00419E90 [0xB628A959]: the script waits (type 6) on the channel with the id 0x5CFCCF35 (the first's
    // without one). 1.
    int WaitOnChannel(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x00419750 [0xAFC2493E]: the channel with the id 0x5CFCCF35 deleted. 1.
    int RemoveChannel(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x004197C0 [0xF1D4B29F]: the channel with the id 0x5CFCCF35 (the first without one) blended (BlendTo with
    // 0x4D786F62 and 0xBF237659, floats). 1.
    int BlendChannel(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x00419F10 [0xD58E2AD5]: whether there's a channel with the id 0x5CFCCF35 (the script's parameters get the
    // flag 0xC7C6228C if so).
    int ChannelExists(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x004198D0 (19 more names, and CallMemberFunction's two): to the channel with the id 0x5CFCCF35 (the first
    // without one; none found: 0), its slot 4.
    int PassToChannel(uint32_t name, Script::CStruct* params, Script::CScript* script);

    // --- the channels ---
    // 0x00419D40: the channel with `id`, or null.
    Gfx::CBlendChannel* FindChannel(uint32_t id);
    // 0x00419FD0: a new channel with `id` (its slot 2 called) at the front or the back. (A failed allocation
    // crashes, as in the original.)
    Gfx::CBlendChannel* AddChannel(uint32_t id, bool atFront);
    // 0x0041A220: slot 1 of each observer, given `pose`.
    void NotifyObservers(Gfx::CPose* pose);
    // 0x004199C0: an empty function (0x005480A0) called on each channel. 0x004199F0: 0x0047E010 on each with
    // `value`. 0x00419A20: 0x0047EF20 on each, then all but the first deleted.
    void VisitChannels();
    void PassToEachChannel(uint32_t value);
    void ResetChannels();
    // 0x00419C10: 0x0047DC40 on each channel with the arguments; whether any said so.
    bool AnyChannel(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
    // 0x00419C70: whether every channel has bit 29 of its +0x54 (true with none).
    bool AllChannelsFlag29();
    // 0x00419CA0: whether the model's +0x208 has flag 0x2 and not 0x4 at its +0x44.
    bool ModelReady();

    // --- the first channel's (nothing without one) ---
    // 0x00419730 (0x0047EFF0), 0x00419A90 (0x004803D0: play `anim`), 0x00419B50 (0x0047F260, with 0 last),
    // 0x00419B90 (0x0047E850), 0x00419BB0 (0x0047E810), 0x00419BD0 (0x0047E280), 0x00419BF0 (0x0047E450; not a
    // Ghidra function), 0x00419B30 (SetFlag4C).
    void FirstChannel47EFF0();
    void PlayAnimation(uint32_t anim, uint32_t b);
    void PlayAnimationEx(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t g);
    void FirstChannel47E850(float a, float b, float c, float d);
    void FirstChannel47E810(uint32_t a);
    void FirstChannel47E280(float a, uint32_t b);
    void FirstChannel47E450(float* a, uint32_t b);
    void SetFlag4C(bool value);
    // 0x00419AB0 / 0x00419AD0 (not a Ghidra function): its +0x50 (the anim set) / +0x58; 0 without one.
    uint32_t GetAnimSet();
    uint32_t GetFirstChannel58();
    // 0x00419AF0 / 0x00419B10: its GetValue (0 without one) / HasValue (false without one).
    float GetValue(uint32_t offset);
    bool HasValue(uint32_t offset);
    // 0x00419CD0: whether its animation is complete (0x0047E210; false without one).
    bool IsAnimComplete();
    // 0x00419F80 / 0x0041A0A0: its first timeline's +0x18 (a float; 0 without one) / +0x30.
    float GetTimeline18();
    uint32_t GetTimeline30();
    // 0x0041A0E0: its first timeline's start, current and end times (0x004813B0; 0 without a timeline; nothing
    // written without a channel).
    void GetTimes(float* start, float* current, float* end);

    CBaseComponent* mp_component18;   // +0x18 the object's 0xCE0CA665 component
    CBaseComponent* mp_skeleton;      // +0x1C its 0x222756D5 component
    CBaseComponent* mp_model;         // +0x20 its 0x286A8D26 component
    uint32_t m_unknown24;             // +0x24 (given to the 0xCE0CA665 component's 0x00433EA0 each update)
    Mem::ListNode m_channels;         // +0x28 the channel list's head (vtable 0x00645D88; data -1)
    void* mp_observers;               // +0x3C a std::list of observers: its sentinel node (+4 next, +8 prev)
    uint32_t m_observerCount;         // +0x40
};
static_assert(offsetof(CAnimBlenderComponent, mp_component18) == 0x18 &&
                  offsetof(CAnimBlenderComponent, m_channels) == 0x28 &&
                  offsetof(CAnimBlenderComponent, mp_observers) == 0x3C && sizeof(CAnimBlenderComponent) == 0x44,
              "layout must match the original");

// Pieces of the channel list's links and of the channel itself kept here.
struct BlendChannelLinks {
    // 0x00419D70: a channel list node's destructor body (unlinked; vtable 0x00645D84). 0x00419D90: its scalar
    // deleting destructor. 0x0041A170: the head's (vtable 0x00645D88).
    void DestroyBody();
    BlendChannelLinks* DestroyNode(unsigned int flags);
    BlendChannelLinks* DestroyHead(unsigned int flags);
    // 0x00419CF0 (on a channel): its first timeline's start, current and end times (0x004813B0; 0 without one).
    void GetTimes(float* start, float* current, float* end);

    Mem::ListNode m_link;
};

// 0x004196B0 (cdecl): the larger of `a` and `b` (`b` when they're equal or NaN).
float __cdecl LargerOf(float a, float b);

// 0x0041A680: the Vector at +0x1C of the object it's called on (a component the debug-info code at 0x00421F18
// asks).
struct VectorAt1C {
    Mth::Vector Get() const;

    uint8_t m_unknown00[0x1C];
    Mth::Vector m_vector;  // +0x1C
};
static_assert(offsetof(VectorAt1C, m_vector) == 0x1C, "layout must match the original");

} // namespace Obj
