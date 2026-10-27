// The animation blender component and the pieces of its channels' links kept with it.

#include "game/obj/anim_blender.h"

#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/anim_blend.h"
#include "game/net/buddy_status.h"
#include "game/obj/movie_manager.h"
#include "game/obj/suspend_component.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/skater/thaw_bmx.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x00645D8C);
void** const kHeadVtable = reinterpret_cast<void**>(0x00645D88);
void** const kNodeVtable = reinterpret_cast<void**>(0x00645D84);

// The blend's shared buffers: the weights, the bone set of each channel, the pose.
float*& s_blendWeights = GameVariable<float*>(0x006F8A5C);
uint32_t*& s_blendSets = GameVariable<uint32_t*>(0x006F8A58);
Gfx::CPose*& s_blendPose = GameVariable<Gfx::CPose*>(0x006F8A60);

constexpr uint32_t kId = 0x5CFCCF35; // SubsetId
constexpr uint32_t kPlayAnim = 0x0B1E7291, kObjPlayAnim = 0x2CB9AD09;
constexpr float kMinWeight = 0.00001f;

bool IsHead(const Mem::ListNode* node) { return node->data == reinterpret_cast<void*>(-1); }

using Channel = Gfx::CBlendChannel;
Mem::ListNode* Link(Channel* channel) { return reinterpret_cast<Mem::ListNode*>(channel); }
Channel* AsChannel(Mem::ListNode* node) { return reinterpret_cast<Channel*>(node); }
// A node as the original masks it: null when it's the head.
Channel* Masked(Mem::ListNode* node) { return IsHead(node) ? nullptr : AsChannel(node); }
// The next channel after `channel` (or the head).
Mem::ListNode* Next(Channel* channel) { return Link(channel)->next; }
// The walks' end test: the head or null.
bool Ended(Mem::ListNode* node) { return IsHead(node) || !node; }

template <typename T>
T& Field(void* object, int offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}
uint32_t& Id(Channel* channel) { return Field<uint32_t>(channel, 0x28); }

// A channel's virtual functions.
template <typename R, typename... A>
R Virtual(void* object, int slot, A... args)
{
    void** vtable = *static_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, A...)>(vtable[slot])(object, args...);
}

// The channel's methods (most not rewritten yet).
template <typename R, typename... A>
R ChannelMethod(uint32_t address, Channel* channel, A... args)
{
    return GameFunction<R(__thiscall*)(Channel*, A...)>(address)(channel, args...);
}

} // namespace

// 0x0041A5B0
CBaseComponent* __cdecl CAnimBlenderComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CAnimBlenderComponent));
    return memory ? static_cast<CAnimBlenderComponent*>(memory)->Construct() : nullptr;
}

// 0x00419690
void __cdecl CAnimBlenderComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), &GameVariable<uint32_t>(0x00682350), 0x18);
}

// 0x0041A410
CAnimBlenderComponent* CAnimBlenderComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = kVtable;
    m_channels.data = reinterpret_cast<void*>(-1);
    m_channels.priority = 0;
    m_channels.next = &m_channels;
    m_channels.prev = &m_channels;
    m_channels.vtable = kHeadVtable;
    m_observerCount = 0;
    auto* sentinel = static_cast<void**>(Mem::New(0xC));
    if (sentinel) {
        sentinel[1] = sentinel;
        sentinel[2] = sentinel;
    }
    mp_observers = sentinel;
    m_type = kType;
    return this;
}

// 0x0041A4C0
void CAnimBlenderComponent::DestroyBody()
{
    m_vtable = kVtable;
    reinterpret_cast<ListHead*>(&m_channels)->DeleteAllNodes();
    auto** sentinel = static_cast<void**>(mp_observers);
    for (auto** node = sentinel ? static_cast<void**>(sentinel[1]) : nullptr; node != mp_observers;
         node = static_cast<void**>(node[1])) {
        if (node[0])
            Virtual<void*>(node[0], 0, 1u);
    }
    // the list emptied (std::list::erase(begin, end), 0x004ECC30) - twice, as the original
    for (int pass = 0; pass < 2; ++pass) {
        auto** head = static_cast<void**>(mp_observers);
        void* result;
        GameFunction<void*(__thiscall*)(void*, void**, void*, void*)>(0x004ECC30)(&mp_observers, &result,
                                                                               head ? head[1] : nullptr, head);
    }
    Mem::Manager::Get()->Free(mp_observers);
    m_channels.UnlinkAs(kNodeVtable);
    CBaseComponent::DestroyBody();
}

// 0x0041A610
CAnimBlenderComponent* CAnimBlenderComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0041A260 (the 0xCE0CA665 component's answer goes to each channel as a dword whose high byte, as in the
// original, may not be clear - only its low byte is meant)
void CAnimBlenderComponent::Update()
{
    uint32_t highestSet = 0;
    int count = 0;
    bool changed = false;
    Channel* channel = Masked(m_channels.next);
    bool ready = static_cast<CSuspendComponent*>(mp_component18)->ShouldAnimate(reinterpret_cast<float*>(&m_unknown24));
    if (!channel)
        return;
    do {
        if (!(Field<float>(channel, 0x30) >= kMinWeight) && count != 0) {
            // a channel after the first with (next to) no weight: only reset
            ChannelMethod<void>(0x0047E030, channel);
            ChannelMethod<void>(0x0047E1B0, channel);
            s_blendSets[count] = Field<uint32_t>(channel, 0x2C);
            if (!(highestSet > s_blendSets[count]))
                highestSet = s_blendSets[count];
            s_blendWeights[count] = 0.0f;
        } else {
            if (Virtual<bool>(channel, 1, count, static_cast<uint32_t>(ready)))
                changed = true;
            s_blendWeights[count] = Field<float>(channel, 0x30);
            s_blendSets[count] = Field<uint32_t>(channel, 0x2C);
            if (!(highestSet > s_blendSets[count]))
                highestSet = s_blendSets[count];
        }
        Mem::ListNode* next = Next(channel);
        ++count;
        if (Ended(next))
            break;
        channel = AsChannel(next);
    } while (true);
    if (!changed)
        return;
    // (the skeleton component's +0x18, through the getter folded at 0x005480F0)
    auto skeleton = [this]() { return reinterpret_cast<void*>(reinterpret_cast<GameNet::FoldedGetters*>(mp_skeleton)->Word18()); };
    if (count == 1) {
        NotifyObservers(s_blendPose);
    } else {
        uint32_t sets = highestSet + 1;
        for (uint32_t set = 0; set != sets;) {
            Gfx::CPose* pose = s_blendPose;
            uint32_t bones = GameFunction<uint32_t(__thiscall*)(void*)>(0x004A2BA0)(skeleton());
            Gfx::BlendPosesById(s_blendPose, s_blendWeights, s_blendSets, static_cast<int>(bones), pose, count, set);
            ++set;
            s_blendSets[0] = set;
            s_blendWeights[0] = 0.0f;
            if (!(set < sets))
                break;
        }
        NotifyObservers(s_blendPose);
    }
    GameFunction<void(__thiscall*)(void*, Gfx::CPose*)>(0x004A44E0)(skeleton(), s_blendPose);
}

// 0x00419DD0
void CAnimBlenderComponent::InitFromStructure(Script::CStruct* params)
{
    if (m_channels.prev != &m_channels)
        return;
    void* memory = Mem::Calloc(0x74);
    Channel* channel = memory ? GameFunction<Channel*(__thiscall*)(void*, CCompositeObject*, CAnimBlenderComponent*,
                                                                    int)>(0x0047F150)(memory, m_object, this, 1)
                              : nullptr;
    Link(channel)->prev = &m_channels;
    Link(channel)->next = m_channels.next;
    m_channels.next->prev = Link(channel);
    m_channels.next = Link(channel);
    Mem::PushContext(Mem::BottomUpHeap());
    Mem::ListNode* first = m_channels.next;
    if (!Ended(first)) {
        Virtual<void>(first, 3, params);
        ChannelMethod<void>(0x0047EA80, AsChannel(first), 1);
    }
    Mem::PopContext();
}

// 0x004196D0
void CAnimBlenderComponent::Finalize()
{
    for (Mem::ListNode* node = m_channels.next; !Ended(node); node = node->next)
        Virtual<void>(node, 2);
    mp_skeleton = m_object->GetComponent(0x222756D5);
    mp_model = m_object->GetComponent(0x286A8D26);
    mp_component18 = m_object->GetComponent(0xCE0CA665);
}

// 0x00419980
void CAnimBlenderComponent::PassToChannels(uint32_t value, uint32_t id)
{
    for (Mem::ListNode* node = m_channels.next; !Ended(node); node = node->next) {
        if (Id(AsChannel(node)) == id)
            Virtual<void>(node, 6, value);
    }
}

// 0x00419950
int CAnimBlenderComponent::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script)
{
    if (name != 0x0B1E7291 && name != 0xEA6D0EFD)
        return 2;
    return PassToChannel(name, params, script);
}

// 0x0041A1A0 (no channel to take the anim set from: a crash, as in the original)
int CAnimBlenderComponent::AddChannelFromScript(uint32_t, Script::CStruct* params, Script::CScript*)
{
    uint32_t id = 0;
    Channel* first = Masked(m_channels.next);
    params->GetChecksum(kId, &id, 0);
    params->AddChecksum(0x52A864B0, first->m_key);
    bool atFront = params->ContainsFlag(0x580C0963);
    Channel* channel = AddChannel(id, atFront);
    Virtual<void>(channel, 3, params);
    Virtual<void>(channel, 2);
    return 1;
}

// 0x00419E90 (the channel with the id is looked for but the result isn't used; no id and no channel: a crash)
int CAnimBlenderComponent::WaitOnChannel(uint32_t, Script::CStruct* params, Script::CScript* script)
{
    uint32_t id = 0;
    Channel* first = Masked(m_channels.next);
    uint32_t waitFor;
    if (params && (params->GetChecksum(kId, &id, 0), id != 0))
        waitFor = id;
    else
        waitFor = Id(first);
    script->SetWaitOnComponent(static_cast<Script::EWaitType>(6), this, waitFor);
    return 1;
}

// 0x00419750
int CAnimBlenderComponent::RemoveChannel(uint32_t, Script::CStruct* params, Script::CScript*)
{
    uint32_t id = 0;
    Channel* channel = Masked(m_channels.next);
    params->GetChecksum(kId, &id, 0);
    if (!channel)
        return 1;
    while (Id(channel) != id) {
        Mem::ListNode* next = Next(channel);
        if (Ended(next))
            return 1;
        channel = AsChannel(next);
    }
    Virtual<void*>(channel, 0, 1u);
    return 1;
}

// 0x004197C0 (no id and no channel: BlendTo on null, a crash, as in the original)
int CAnimBlenderComponent::BlendChannel(uint32_t, Script::CStruct* params, Script::CScript*)
{
    uint32_t id = 0;
    Channel* channel = Masked(m_channels.next);
    params->GetChecksum(kId, &id, 0);
    if (id != 0) {
        if (!channel)
            return 1;
        while (Id(channel) != id) {
            Mem::ListNode* next = Next(channel);
            if (Ended(next))
                return 1;
            channel = AsChannel(next);
        }
    }
    float a = 0.0f, b = 0.0f;
    params->GetFloat(0x4D786F62, &a, 1);
    params->GetFloat(0xBF237659, &b, 0);
    uint32_t target; // (BlendTo's target is a float here: its bits passed on)
    std::memcpy(&target, &a, sizeof(target));
    channel->BlendTo(target, b);
    return 1;
}

// 0x00419F10 (not a Ghidra function)
int CAnimBlenderComponent::ChannelExists(uint32_t, Script::CStruct* params, Script::CScript* script)
{
    uint32_t id = 0;
    params->GetChecksum(kId, &id, 0);
    for (Mem::ListNode* node = m_channels.next; !Ended(node); node = node->next) {
        if (Id(AsChannel(node)) == id) {
            script->m_params->AddChecksum(0u, 0xC7C6228C); // (an unnamed flag)
            return 1;
        }
    }
    return 0;
}

// 0x004198D0 (no id given and no channel: a call on null, a crash, as in the original)
int CAnimBlenderComponent::PassToChannel(uint32_t name, Script::CStruct* params, Script::CScript* script)
{
    uint32_t id = 0;
    Channel* channel = Masked(m_channels.next);
    if (params && (params->GetChecksum(kId, &id, 0), id != 0)) {
        if (!channel)
            return 0;
        while (Id(channel) != id) {
            Mem::ListNode* next = Next(channel);
            if (Ended(next))
                return 0;
            channel = AsChannel(next);
        }
    }
    // NOT ORIGINAL - THAW's BMX ([features] thaw_bmx; the local rider on the bike and his bike): THAW's animation tree
    // flips a branch as it's played (Skater_Playanim's obj_is_flipped), so the channel takes the rider's flipped flag
    // before it plays - on the bike his Flip turns the flag only (game/skater/thaw_bmx.h PlayFlip). 0x0047DC40 with
    // its last argument 0: the flag set, nothing sent. Original behaviour: the channel's flip as it was.
    if ((name == kPlayAnim || name == kObjPlayAnim) && channel && Skater::ThawBmx::Enabled()) {
        int flip = Skater::ThawBmx::PlayFlip(m_object);
        if (flip >= 0)
            ChannelMethod<bool>(0x0047DC40, channel, m_object->m_id, static_cast<uint32_t>(flip), 0u, 0u);
    }
    return Virtual<int>(channel, 4, name, params, script);
}

// 0x00419D40
Gfx::CBlendChannel* CAnimBlenderComponent::FindChannel(uint32_t id)
{
    for (Mem::ListNode* node = m_channels.next; !Ended(node); node = node->next) {
        if (Id(AsChannel(node)) == id)
            return AsChannel(node);
    }
    return nullptr;
}

// 0x00419FD0 (the original first looks for a channel with the id and drops the result)
Gfx::CBlendChannel* CAnimBlenderComponent::AddChannel(uint32_t id, bool atFront)
{
    void* memory = Mem::Calloc(0x74);
    Channel* channel = memory ? GameFunction<Channel*(__thiscall*)(void*, CCompositeObject*, CAnimBlenderComponent*,
                                                                    int)>(0x0047F150)(memory, m_object, this, 0)
                              : nullptr;
    Id(channel) = id;
    Virtual<void>(channel, 2);
    Mem::ListNode* link = Link(channel);
    if (atFront) {
        link->prev = &m_channels;
        link->next = m_channels.next;
        m_channels.next->prev = link;
        m_channels.next = link;
    } else {
        link->prev = m_channels.prev;
        link->next = &m_channels;
        m_channels.prev->next = link;
        m_channels.prev = link;
    }
    return channel;
}

// 0x0041A220
void CAnimBlenderComponent::NotifyObservers(Gfx::CPose* pose)
{
    auto** sentinel = static_cast<void**>(mp_observers);
    for (auto** node = sentinel ? static_cast<void**>(sentinel[1]) : nullptr; node != mp_observers;
         node = static_cast<void**>(node[1]))
        Virtual<void>(node[0], 1, pose);
}

// 0x004199C0
void CAnimBlenderComponent::VisitChannels()
{
    for (Mem::ListNode* node = m_channels.next; !Ended(node); node = node->next) {
        // (0x005480A0: an empty function)
    }
}

// 0x004199F0
void CAnimBlenderComponent::PassToEachChannel(uint32_t value)
{
    for (Mem::ListNode* node = m_channels.next; !Ended(node); node = node->next)
        ChannelMethod<void>(0x0047E010, AsChannel(node), value);
}

// 0x00419A20 (with no channel, the original reads the head's next from null: a crash)
void CAnimBlenderComponent::ResetChannels()
{
    for (Mem::ListNode* node = m_channels.next; !Ended(node); node = node->next)
        ChannelMethod<void>(0x0047EF20, AsChannel(node));
    Mem::ListNode* node = Link(Masked(m_channels.next))->next;
    if (Ended(node))
        return;
    while (node) {
        Mem::ListNode* next = node->next;
        Mem::ListNode* following = IsHead(next) ? nullptr : next;
        Virtual<void*>(node, 0, 1u);
        node = following;
    }
}

// 0x00419C10
bool CAnimBlenderComponent::AnyChannel(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    bool any = false;
    for (Mem::ListNode* node = m_channels.next; !Ended(node); node = node->next) {
        if (ChannelMethod<bool>(0x0047DC40, AsChannel(node), a, b, c, d))
            any = true;
    }
    return any;
}

// 0x00419C70
bool CAnimBlenderComponent::AllChannelsFlag29()
{
    bool all = true;
    for (Mem::ListNode* node = m_channels.next; !Ended(node); node = node->next)
        all = all && ((Field<uint32_t>(node, 0x54) >> 29) & 1);
    return all;
}

// 0x00419CA0
bool CAnimBlenderComponent::ModelReady()
{
    void* thing = Field<void*>(mp_model, 0x208);
    if (!thing)
        return false;
    uint8_t flags = Field<uint8_t>(thing, 0x44);
    return (flags & 2) && !(flags & 4);
}

// 0x00419730
void CAnimBlenderComponent::FirstChannel47EFF0()
{
    if (Channel* channel = Masked(m_channels.next))
        ChannelMethod<void>(0x0047EFF0, channel);
}

// 0x00419A90
void CAnimBlenderComponent::PlayAnimation(uint32_t anim, uint32_t b)
{
    if (Channel* channel = Masked(m_channels.next))
        ChannelMethod<void>(0x004803D0, channel, anim, b);
}

// 0x00419B50
void CAnimBlenderComponent::PlayAnimationEx(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f,
                                            uint32_t g)
{
    if (Channel* channel = Masked(m_channels.next))
        ChannelMethod<void>(0x0047F260, channel, a, b, c, d, e, f, g, 0u);
}

// 0x00419B90
void CAnimBlenderComponent::FirstChannel47E850(float a, float b, float c, float d)
{
    if (Channel* channel = Masked(m_channels.next))
        ChannelMethod<void>(0x0047E850, channel, a, b, c, d);
}

// 0x00419BB0
void CAnimBlenderComponent::FirstChannel47E810(uint32_t a)
{
    if (Channel* channel = Masked(m_channels.next))
        ChannelMethod<void>(0x0047E810, channel, a);
}

// 0x00419BD0
void CAnimBlenderComponent::FirstChannel47E280(float a, uint32_t b)
{
    if (Channel* channel = Masked(m_channels.next))
        ChannelMethod<void>(0x0047E280, channel, a, b);
}

// 0x00419BF0
void CAnimBlenderComponent::FirstChannel47E450(float* a, uint32_t b)
{
    if (Channel* channel = Masked(m_channels.next))
        ChannelMethod<void>(0x0047E450, channel, a, b);
}

// 0x00419B30
void CAnimBlenderComponent::SetFlag4C(bool value)
{
    if (Channel* channel = Masked(m_channels.next))
        channel->SetFlag4C(value);
}

// 0x00419AB0
uint32_t CAnimBlenderComponent::GetAnimSet()
{
    Channel* channel = Masked(m_channels.next);
    return channel ? channel->m_key : 0;
}

// 0x00419AD0
uint32_t CAnimBlenderComponent::GetFirstChannel58()
{
    Channel* channel = Masked(m_channels.next);
    return channel ? Field<uint32_t>(channel, 0x58) : 0;
}

// 0x00419AF0
float CAnimBlenderComponent::GetValue(uint32_t offset)
{
    Channel* channel = Masked(m_channels.next);
    return channel ? channel->GetValue(offset) : 0.0f;
}

// 0x00419B10
bool CAnimBlenderComponent::HasValue(uint32_t offset)
{
    Channel* channel = Masked(m_channels.next);
    return channel ? channel->HasValue(offset) : false;
}

// 0x00419CD0
bool CAnimBlenderComponent::IsAnimComplete()
{
    Channel* channel = Masked(m_channels.next);
    return channel ? ChannelMethod<bool>(0x0047E210, channel) : false;
}

// 0x00419F80
float CAnimBlenderComponent::GetTimeline18()
{
    Channel* channel = Masked(m_channels.next);
    if (!channel)
        return 0.0f;
    auto* timeline = Field<Mem::ListNode*>(channel, 0x20);
    if (Ended(timeline))
        return 0.0f;
    return Field<float>(timeline, 0x18);
}

// 0x0041A0A0
uint32_t CAnimBlenderComponent::GetTimeline30()
{
    Channel* channel = Masked(m_channels.next);
    if (!channel)
        return 0;
    auto* timeline = Field<Mem::ListNode*>(channel, 0x20);
    if (Ended(timeline))
        return 0;
    return Field<uint32_t>(timeline, 0x30);
}

// 0x0041A0E0
void CAnimBlenderComponent::GetTimes(float* start, float* current, float* end)
{
    Channel* channel = Masked(m_channels.next);
    if (!channel)
        return;
    float s = 0.0f, c = 0.0f, e = 0.0f;
    auto* timeline = Field<Mem::ListNode*>(channel, 0x20);
    if (!Ended(timeline))
        GameFunction<void(__thiscall*)(void*, float*, float*, float*)>(0x004813B0)(timeline, &s, &c, &e);
    *start = s;
    *current = c;
    *end = e;
}

// 0x00419D70
void BlendChannelLinks::DestroyBody() { m_link.UnlinkAs(kNodeVtable); }

// 0x00419D90
BlendChannelLinks* BlendChannelLinks::DestroyNode(unsigned int flags)
{
    m_link.UnlinkAs(kNodeVtable);
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0041A170
BlendChannelLinks* BlendChannelLinks::DestroyHead(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00419CF0
void BlendChannelLinks::GetTimes(float* start, float* current, float* end)
{
    *start = 0.0f;
    *current = 0.0f;
    *end = 0.0f;
    auto* timeline = Field<Mem::ListNode*>(this, 0x20);
    if (!Ended(timeline))
        GameFunction<void(__thiscall*)(void*, float*, float*, float*)>(0x004813B0)(timeline, start, current, end);
}

// 0x004196B0
float __cdecl LargerOf(float a, float b) { return a > b ? a : b; }

// 0x0041A680
Mth::Vector VectorAt1C::Get() const { return m_vector; }

} // namespace Obj

REPLACE(0x0041A5B0, Obj::CAnimBlenderComponent::Create);
REPLACE(0x00419690, Obj::CAnimBlenderComponent::RegisterMemberFunctions);
REPLACE(0x0041A410, &Obj::CAnimBlenderComponent::Construct);
REPLACE(0x0041A4C0, &Obj::CAnimBlenderComponent::DestroyBody);
REPLACE(0x0041A610, &Obj::CAnimBlenderComponent::Destroy);
REPLACE(0x0041A260, &Obj::CAnimBlenderComponent::Update);
REPLACE(0x00419DD0, &Obj::CAnimBlenderComponent::InitFromStructure);
REPLACE(0x004196D0, &Obj::CAnimBlenderComponent::Finalize);
REPLACE(0x00419980, &Obj::CAnimBlenderComponent::PassToChannels);
REPLACE(0x00419950, &Obj::CAnimBlenderComponent::CallMemberFunction);
REPLACE(0x0041A1A0, &Obj::CAnimBlenderComponent::AddChannelFromScript);
REPLACE(0x00419E90, &Obj::CAnimBlenderComponent::WaitOnChannel);
REPLACE(0x00419750, &Obj::CAnimBlenderComponent::RemoveChannel);
REPLACE(0x004197C0, &Obj::CAnimBlenderComponent::BlendChannel);
REPLACE(0x00419F10, &Obj::CAnimBlenderComponent::ChannelExists);
REPLACE(0x004198D0, &Obj::CAnimBlenderComponent::PassToChannel);
REPLACE(0x00419D40, &Obj::CAnimBlenderComponent::FindChannel);
REPLACE(0x00419FD0, &Obj::CAnimBlenderComponent::AddChannel);
REPLACE(0x0041A220, &Obj::CAnimBlenderComponent::NotifyObservers);
REPLACE(0x004199C0, &Obj::CAnimBlenderComponent::VisitChannels);
REPLACE(0x004199F0, &Obj::CAnimBlenderComponent::PassToEachChannel);
REPLACE(0x00419A20, &Obj::CAnimBlenderComponent::ResetChannels);
REPLACE(0x00419C10, &Obj::CAnimBlenderComponent::AnyChannel);
REPLACE(0x00419C70, &Obj::CAnimBlenderComponent::AllChannelsFlag29);
REPLACE(0x00419CA0, &Obj::CAnimBlenderComponent::ModelReady);
REPLACE(0x00419730, &Obj::CAnimBlenderComponent::FirstChannel47EFF0);
REPLACE(0x00419A90, &Obj::CAnimBlenderComponent::PlayAnimation);
REPLACE(0x00419B50, &Obj::CAnimBlenderComponent::PlayAnimationEx);
REPLACE(0x00419B90, &Obj::CAnimBlenderComponent::FirstChannel47E850);
REPLACE(0x00419BB0, &Obj::CAnimBlenderComponent::FirstChannel47E810);
REPLACE(0x00419BD0, &Obj::CAnimBlenderComponent::FirstChannel47E280);
REPLACE(0x00419BF0, &Obj::CAnimBlenderComponent::FirstChannel47E450);
REPLACE(0x00419B30, &Obj::CAnimBlenderComponent::SetFlag4C);
REPLACE(0x00419AB0, &Obj::CAnimBlenderComponent::GetAnimSet);
REPLACE(0x00419AD0, &Obj::CAnimBlenderComponent::GetFirstChannel58);
REPLACE(0x00419AF0, &Obj::CAnimBlenderComponent::GetValue);
REPLACE(0x00419B10, &Obj::CAnimBlenderComponent::HasValue);
REPLACE(0x00419CD0, &Obj::CAnimBlenderComponent::IsAnimComplete);
REPLACE(0x00419F80, &Obj::CAnimBlenderComponent::GetTimeline18);
REPLACE(0x0041A0A0, &Obj::CAnimBlenderComponent::GetTimeline30);
REPLACE(0x0041A0E0, &Obj::CAnimBlenderComponent::GetTimes);
REPLACE(0x00419D70, &Obj::BlendChannelLinks::DestroyBody);
REPLACE(0x00419D90, &Obj::BlendChannelLinks::DestroyNode);
REPLACE(0x0041A170, &Obj::BlendChannelLinks::DestroyHead);
REPLACE(0x00419CF0, &Obj::BlendChannelLinks::GetTimes);
REPLACE(0x004196B0, Obj::LargerOf);
REPLACE(0x0041A680, &Obj::VectorAt1C::Get);
