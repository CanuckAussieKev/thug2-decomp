// The animation blender's channel (Gfx::CBlendChannel, anim_blend.h): its timelines, blend value, wobble, speed, flip
// and loop messages, poses (0x0047DC40-0x0047F24E). From the disassembly.

#include "game/gfx/anim_blend.h"

#include <cmath>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/gfx/blend_timeline.h"
#include "game/gfx/skeleton.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/player_info.h"
#include "game/obj/anim_blender.h"
#include "game/obj/cutscene.h"
#include "game/obj/movie_manager.h"
#include "game/obj/object.h"
#include "game/obj/skeleton_component.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Gfx {

namespace {

using Script::CStruct;

constexpr uint32_t kChannelVtable = 0x00648714;
constexpr uint32_t kHeadVtable = 0x00648710;
constexpr uint32_t kListNodeVtable = 0x006486CC;
constexpr uint32_t kBlenderNodeVtable = 0x00645D84; // (the blender's channel list node)
constexpr uint32_t kSkeleton = 0x222756D5;          // skeleton
constexpr uint32_t kAnimScratch = 0x7C0B34B4;       // AnimScratch
constexpr uint32_t kFlipped = 0x20000000;
constexpr float kFramesPerSecond = 60.0f;            // 0x00645EA0
constexpr float kSecondsPerFrame = 1.0f / 60.0f;     // 0x006463B8

// The network messages a channel sends.
enum : uint8_t { kMsgWobbleTarget = 0x2B, kMsgFlip = 0x2C, kMsgWobbleDetails = 0x3C, kMsgLoopType = 0x3D,
                 kMsgSpeed = 0x41 };

// What was last sent (all start 0xFF).
uint8_t& LastWobbleTarget() { return GameVariable<uint8_t>(0x0067DC10); }
uint8_t& LastWobbleDetails() { return GameVariable<uint8_t>(0x0067DC11); }
int8_t& LastLoopType() { return GameVariable<int8_t>(0x0067DC12); }

// The blend buffers (InitBuffers).
uint32_t*& BlendSets() { return GameVariable<uint32_t*>(0x006F8A58); }
float*& BlendWeights() { return GameVariable<float*>(0x006F8A5C); }
CPose*& BlendBuffer() { return GameVariable<CPose*>(0x006F8A60); }
CPose*& Poses() { return GameVariable<CPose*>(0x006F8A64); }
int& MaxBlends() { return GameVariable<int>(0x006F8A68); }
int& Subsets() { return GameVariable<int>(0x006F8A6C); }
bool SkipRender() { return GameVariable<bool>(0x007D1EC0); }

Mem::PoolPair& StructPools() { return GameVariable<Mem::PoolPair>(0x006F3614); }

float& AsFloat(uint32_t& bits) { return reinterpret_cast<float&>(bits); }

bool IsHead(const Mem::ListNode* node) { return node->data == reinterpret_cast<void*>(-1); }
// A list node as the original masks it: null when it's the head.
CBlendTimeline* Masked(Mem::ListNode* node) { return IsHead(node) ? nullptr : reinterpret_cast<CBlendTimeline*>(node); }
Mem::ListNode* Node(CBlendTimeline* timeline) { return reinterpret_cast<Mem::ListNode*>(timeline); }

// The playing timeline (the list's first), or null.
CBlendTimeline* First(CBlendChannel* channel)
{
    Mem::ListNode* node = channel->m_timelines.next;
    return IsHead(node) || !node ? nullptr : reinterpret_cast<CBlendTimeline*>(node);
}

void Unlink(Mem::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

template <typename R, typename... A>
R Virtual(void* object, int slot, A... args)
{
    void** vtable = *static_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, A...)>(vtable[slot])(object, args...);
}

// A timeline's slots: 0 the scalar deleting destructor, 2 Update, 5 CallMemberFunction.
void DeleteTimeline(Mem::ListNode* node) { Virtual<void*>(node, 0, 1); }

Obj::CCompositeObject* Owner(CBlendChannel* channel) { return reinterpret_cast<Obj::CCompositeObject*>(channel->mp_owner); }

// The network manager (0x007CCE14) and this machine's client app for the channel's object - null when the object
// isn't this machine's player's skater.
GameNet::Manager* NetManager() { return GameVariable<GameNet::Manager*>(0x007CCE14); }
GameNet::PlayerInfo* LocalPlayer(uint32_t objectId)
{
    auto* player = reinterpret_cast<GameNet::PlayerInfo*>(NetManager()->PlayerByObjectId(static_cast<uint16_t>(objectId)));
    if (!player || !player->IsLocalPlayer())
        return nullptr;
    return player;
}
Net::Client* ClientOf(GameNet::PlayerInfo* player)
{
    return static_cast<Net::Client*>(NetManager()->GetClient(static_cast<int>(player->GetSkaterValue2BC())));
}

void Send(Net::Client* app, Net::MsgDesc& message, uint8_t id, void* data, uint16_t length, bool singular)
{
    message.m_data = data;
    message.m_length = length;
    message.m_id = id;
    if (singular)
        message.m_singular = true;
    app->EnqueueMessageToServer(&message);
}

// A structure made and freed as the original's inlined new / delete do.
CStruct* NewStruct()
{
    void* memory = StructPools().Allocate(8);
    return memory ? static_cast<CStruct*>(memory)->Init() : nullptr;
}
void FreeStruct(CStruct* params)
{
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
}

// The playing timeline's controllers given a member function with `params` (slot 5, no script).
void PassToFirst(CBlendChannel* channel, uint32_t name, CStruct* params)
{
    Virtual<int>(Masked(channel->m_timelines.next), 5, name, params, static_cast<Script::CScript*>(nullptr));
}

// The original's _ftol: truncated towards zero.
int Truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// 0x00481B40 / 0x00481B80 / 0x00481EE0: the blend timeline's StartBlendOut, its weight (ST0) and its operator new
// (not rewritten yet).
bool StartBlendOut(CBlendTimeline* timeline, float period)
{
    return GameFunction<bool(__thiscall*)(CBlendTimeline*, float)>(0x00481B40)(timeline, period);
}
double Weight(CBlendTimeline* timeline) { return GameFunction<double(__thiscall*)(CBlendTimeline*)>(0x00481B80)(timeline); }
void* NewTimeline(uint32_t size, int unused) { return GameFunction<void*(__cdecl*)(uint32_t, int)>(0x00481EE0)(size, unused); }

int CountListItems(Mem::ListNode* head) { return reinterpret_cast<AnimListHead*>(head)->CountItems(); }

// The skeleton component's skeleton (the getter folded at 0x005480F0).
CSkeleton* SkeletonOf(Obj::CBaseComponent* component)
{
    return reinterpret_cast<CSkeleton*>(static_cast<Obj::CSkeletonComponent*>(component)->mp_skeleton);
}

} // namespace

// 0x0047DD10
int AnimListHead::CountItems()
{
    Mem::ListNode* node = m_head.next;
    int count = 0;
    while (!IsHead(node) && node) {
        node = node->next;
        ++count;
    }
    return count;
}

// 0x0047DD40
AnimListNode* AnimListNode::Destroy(uint8_t flags)
{
    m_node.UnlinkAs(reinterpret_cast<void**>(kListNodeVtable));
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// --- making and destroying ---

// 0x0047F150
CBlendChannel* CBlendChannel::Construct(Obj::CCompositeObject* object, Obj::CAnimBlenderComponent* blender, bool full)
{
    m_node.data = this;
    m_node.priority = 0;
    m_node.next = &m_node;
    m_node.prev = &m_node;
    m_node.vtable = reinterpret_cast<void**>(kChannelVtable);
    m_timelines.data = reinterpret_cast<void*>(-1);
    m_timelines.priority = 0;
    m_timelines.next = &m_timelines;
    m_timelines.prev = &m_timelines;
    m_timelines.vtable = reinterpret_cast<void**>(kHeadVtable);
    m_eventsOn = true;
    mp_owner = reinterpret_cast<uint8_t*>(object);
    mp_blender = blender;
    m_key = 0;
    m_eventTable = 0;
    m_id = 0;
    m_priority = 0;
    m_flag4C = false;
    m_periodLeft = 0.0f;
    m_period = 0.0f;
    uint32_t value = full ? 0x3F800000 : 0;
    m_current = value;
    m_target = value;
    m_start = value;
    Reset();
    return this;
}

// 0x0047EE90
void CBlendChannel::DestroyBody()
{
    m_node.vtable = reinterpret_cast<void**>(kChannelVtable);
    reinterpret_cast<Obj::ListHead*>(&m_timelines)->DeleteAllNodes();
    m_timelines.UnlinkAs(reinterpret_cast<void**>(kListNodeVtable));
    m_node.UnlinkAs(reinterpret_cast<void**>(kBlenderNodeVtable));
}

// 0x0047F230
CBlendChannel* CBlendChannel::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x0047EF20
void CBlendChannel::Reset()
{
    reinterpret_cast<Obj::ListHead*>(&m_timelines)->DeleteAllNodes();
    bool waiting = m_waiting;
    m_hasStartTime = false;
    m_startTime = 0.0f;
    if (waiting) {
        m_waiting = false;
        if (Script::CScript* script = Owner(this)->m_script)
            script->StopWaiting();
    }
    m_waitTime = 0.0f;
    m_firstTime = 0.0f;
    m_flag4E = false;
}

// 0x0047DCF0
void CBlendChannel::Finalize()
{
    mp_skeleton = Owner(this)->GetComponent(kSkeleton);
}

// --- each frame ---

// 0x0047EFB0
bool CBlendChannel::Update(int index, bool blend)
{
    UpdateBlend();
    UpdateTimelines();
    RemoveDoneTimelines();
    if (blend && mp_blender->ModelReady())
        return BlendPose(index);
    return false;
}

// 0x0047EF60
void CBlendChannel::ReleaseWaitingScript(Script::CScript* script)
{
    if (script->m_waitType != 6)
        return;
    if (CBlendTimeline* first = First(this)) {
        if (first->IsComplete())
            script->StopWaiting();
    }
}

// 0x0047E030 (x87: the blend's time left unrounded through the division)
void CBlendChannel::UpdateBlend()
{
    if (CBlendTimeline* first = First(this))
        m_firstTime = first->m_current;
    if (m_periodLeft > 0.0f) {
        double left = m_periodLeft - Tmr::GetSlomoFrameSeconds();
        m_periodLeft = static_cast<float>(left);
        if (!(left > 0.0))
            left = 0.0;
        m_periodLeft = static_cast<float>(left);
        double done = left / m_period;
        AsFloat(m_current) =
            static_cast<float>((static_cast<double>(AsFloat(m_start)) - AsFloat(m_target)) * done + AsFloat(m_target));
    } else {
        m_current = m_target;
    }
    if (!m_waiting)
        return;
    Obj::CCompositeObject* owner = Owner(this);
    if (!owner->m_script) {
        void* memory = Script::CScript::operator new(sizeof(Script::CScript));
        owner->m_script = memory ? static_cast<Script::CScript*>(memory)->Construct() : nullptr;
    }
    bool stop;
    if (!owner->m_script->IsBlocked(false)) {
        stop = true;
    } else {
        float start, current, end;
        reinterpret_cast<Obj::BlendChannelLinks*>(this)->GetTimes(&start, &current, &end);
        // (forwards: the time reached; backwards - or a range of none - reached the other way; NaN: never)
        if (start <= end && current >= m_waitTime)
            stop = true;
        else
            stop = start >= end && current <= m_waitTime;
    }
    if (stop) {
        owner->m_script->StopWaiting();
        m_waiting = false;
    }
}

// 0x0047E1B0
void CBlendChannel::UpdateTimelines()
{
    int count = CountListItems(&m_timelines);
    CBlendTimeline* timeline = Masked(m_timelines.next);
    for (; count > 0; --count) {
        Virtual<void>(timeline, 2);
        timeline = Masked(Node(timeline)->next);
    }
}

// 0x0047DDC0
void CBlendChannel::RemoveDoneTimelines()
{
    Mem::ListNode* node = m_timelines.next;
    if (IsHead(node) || !node)
        return;
    while (node) {
        CBlendTimeline* next = Masked(node->next);
        if (!reinterpret_cast<CBlendTimeline*>(node)->IsActive()) {
            Unlink(node);
            DeleteTimeline(node);
        }
        node = Node(next);
    }
}

// 0x0047DE20
void CBlendChannel::PushTimeline(float period)
{
    int count = 0;
    for (CBlendTimeline* timeline = Masked(m_timelines.next); timeline;) {
        CBlendTimeline* next = Masked(Node(timeline)->next);
        if (++count >= MaxBlends()) {
            Unlink(Node(timeline));
            DeleteTimeline(Node(timeline));
        }
        timeline = next;
    }
    if (CountListItems(&m_timelines) != 0) {
        CBlendTimeline* first = First(this);
        if (first && StartBlendOut(first, period)) {
            float others = 0.0f;
            for (Mem::ListNode* node = Node(first)->next; !IsHead(node) && node; node = node->next) {
                auto* timeline = reinterpret_cast<CBlendTimeline*>(node);
                if (timeline->IsBlendingOut())
                    others = static_cast<float>(static_cast<double>(timeline->m_weightScale) * timeline->m_blendLeft +
                                                others);
            }
            first->m_weightScale = static_cast<float>((1.0 - others) / first->m_blendLeft);
        }
    }
    void* memory = NewTimeline(sizeof(CBlendTimeline), 0);
    CBlendTimeline* timeline =
        memory ? static_cast<CBlendTimeline*>(memory)->Construct(Owner(this)) : nullptr;
    Mem::ListNode* node = Node(timeline); // (null: a crash, as the original)
    node->prev = &m_timelines;
    node->next = m_timelines.next;
    m_timelines.next->prev = node;
    m_timelines.next = node;
}

// 0x0047ED60
bool CBlendChannel::BlendPose(int index)
{
    if (SkipRender() || !m_key)
        return false;
    CSkeleton* skeleton = SkeletonOf(mp_skeleton);
    if (!skeleton)
        return false;
    skeleton->SetLOD(*reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(mp_blender) + 0x24)); // (its m_unknown24)
    int count = CountListItems(&m_timelines);
    CSkeletonData* data = skeleton->mp_data;
    for (int i = 0; i < count; ++i) {
        // (each pose's translations start as the skeleton's bind translations)
        std::memcpy(Poses()[i].translations, data->mp_bindTranslations,
                    static_cast<size_t>(skeleton->GetNumBones() & 0x0FFFFFFF) * 16);
        GetPose(i, &Poses()[i], &BlendWeights()[i]);
    }
    if (count > 1)
        BlendPoses(Poses(), BlendWeights(), skeleton->GetNumBones(), Poses(), count);
    std::memcpy(&BlendBuffer()[index], Poses(), sizeof(CPose));
    return true;
}

// 0x0047EFF0
bool CBlendChannel::BlendFirstPose()
{
    return BlendPose(0);
}

// 0x0047EAA0
void CBlendChannel::GetPose(int index, void* pose, float* weight)
{
    CBlendTimeline* timeline = nullptr;
    Mem::ListNode* node = m_timelines.next;
    if (!IsHead(node) && node) {
        for (;;) {
            if (index-- == 0) {
                timeline = reinterpret_cast<CBlendTimeline*>(node);
                break;
            }
            node = node->next;
            if (IsHead(node) || !node)
                break;
        }
    }
    double w = Weight(timeline); // (null when not found: a crash, as the original)
    float rounded = static_cast<float>(w);
    if (!(w <= 0.0)) {
        SkeletonOf(mp_skeleton); // (read and not used)
        timeline->GetPoses(pose);
        *weight = rounded;
    } else {
        *weight = 0.0f;
    }
}

// --- what scripts and the network ask ---

// 0x0047DC40
bool CBlendChannel::SetFlipped(uint32_t objectId, bool flip, uint32_t /*unused*/, bool send)
{
    if (send && m_id == 0) {
        if (GameNet::PlayerInfo* player = LocalPlayer(objectId)) {
            Net::MsgDesc message;
            message.Construct();
            Net::Client* app = ClientOf(player);
            uint8_t data[2] = {static_cast<uint8_t>(objectId), static_cast<uint8_t>(flip)};
            Send(app, message, kMsgFlip, data, 2, false);
        }
    }
    bool was = (m_flags >> 29) & 1;
    m_flags = (m_flags & ~kFlipped) | (flip ? kFlipped : 0);
    return was;
}

// 0x0047DD80
void CBlendChannel::ToggleFlipped()
{
    Net::App* app = NetManager()->GetClient(0);
    uint32_t unused = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(app) + 8);
    SetFlipped(Owner(this)->m_id, !((m_flags >> 29) & 1), unused, true);
}

// 0x0047E010
void CBlendChannel::AddTime(float frames)
{
    Mem::ListNode* node = m_timelines.next;
    if (!IsHead(node) && node)
        reinterpret_cast<CBlendTimeline*>(node)->AddTime(frames);
}

// 0x0047E210
bool CBlendChannel::IsAnimComplete()
{
    CBlendTimeline* first = First(this);
    return first ? first->IsComplete() : false;
}

// 0x0047E240
bool CBlendChannel::IsCycling()
{
    CBlendTimeline* first = First(this);
    if (!first)
        return false;
    return first->m_loopType == CAnimTimeline::kCycle || first->m_loopType == CAnimTimeline::kPingPong;
}

// 0x0047DF50
bool CBlendChannel::IsAnimOneOf(CStruct* params, Script::CScript* /*script*/)
{
    CBlendTimeline* first = First(this);
    uint32_t anim = first ? first->m_anim : 0;
    for (Script::CComponent* component = params->GetNextComponent(nullptr); component;
         component = params->GetNextComponent(component)) {
        if (component->name)
            continue;
        Script::CArray* array = nullptr;
        if (component->Type() == Script::ESYMBOLTYPE_NAME) {
            Script::CSymbolTableEntry* symbol = Script::LookUpSymbol(component->checksum);
            if (symbol && symbol->type == Script::ESYMBOLTYPE_ARRAY) {
                array = reinterpret_cast<Script::CArray*>(symbol->raw);
            } else {
                if (component->checksum == anim)
                    return true;
                continue;
            }
        } else if (component->Type() == Script::ESYMBOLTYPE_ARRAY) {
            array = component->array;
        } else {
            continue;
        }
        if (array) {
            for (uint32_t i = 0; i < array->m_size; ++i) {
                if (array->GetElement(i) == anim)
                    return true;
            }
        }
    }
    return false;
}

// 0x0047E280
void CBlendChannel::SetWobbleTarget(float target, bool send)
{
    if (send && m_id == 0) {
        if (GameNet::PlayerInfo* player = LocalPlayer(Owner(this)->m_id)) {
            Net::Client* app = ClientOf(player);
            if (target < 0.0f)
                target = 0.0f;
            else if (target > 1.0f)
                target = 1.0f;
            uint8_t data[2];
            data[1] = static_cast<uint8_t>(Truncated(target * 255.0)); // (0x00646B88)
            data[0] = static_cast<uint8_t>(Owner(this)->m_id);
            if (LastWobbleTarget() != data[1]) {
                Net::MsgDesc message;
                message.Construct();
                Send(app, message, kMsgWobbleTarget, data, 2, true);
                LastWobbleTarget() = data[1];
            }
        }
    }
    if (!First(this))
        return;
    CStruct* params = NewStruct();
    params->AddFloat(0x4D747FA0, target);
    PassToFirst(this, 0xD0209498, params); // SetWobbleTarget
    FreeStruct(params);
}

// 0x0047E450
void CBlendChannel::SetWobbleDetails(const float* details, bool send)
{
    if (send && m_id == 0) {
        if (GameNet::PlayerInfo* player = LocalPlayer(Owner(this)->m_id)) {
            Net::Client* app = ClientOf(player);
            // Which of its known values each detail has (0x00645D6C... - the server sends those).
            uint8_t bits = 0;
            if (details[0] == 0.05f)
                bits = 0;
            else if (details[0] == 0.1f)
                bits = 1;
            else if (details[0] == 10.1f)
                bits = 2;
            uint8_t b1 = details[1] == 0.04f ? 0 : details[1] == 10.1f ? 1 : 0;
            uint8_t b2 = details[2] == 0.0022f ? 0 : details[2] == 20.0f ? 1 : 0;
            uint8_t b3 = details[3] == 0.0017f ? 0 : details[3] == 10.0f ? 1 : 0;
            uint8_t b4 = details[4] == 1.5f ? 0 : details[4] == 1.0f ? 1 : 0;
            uint8_t data[2];
            data[0] = static_cast<uint8_t>(bits | b1 << 2 | b2 << 3 | b3 << 4 | b4 << 5);
            data[1] = static_cast<uint8_t>(Owner(this)->m_id);
            if (LastWobbleDetails() != data[0]) {
                Net::MsgDesc message;
                message.Construct();
                Send(app, message, kMsgWobbleDetails, data, 2, true);
                LastWobbleDetails() = data[0];
            }
        }
    }
    if (!First(this))
        return;
    CStruct* params = NewStruct();
    params->AddFloat(0xFD266A26, details[0]); // wobbleAmpA
    params->AddFloat(0x642F3B9C, details[1]); // wobbleAmpB
    params->AddFloat(0x0F43FD49, details[2]); // wobbleK1
    params->AddFloat(0x964AACF3, details[3]); // wobbleK2
    params->AddFloat(0xF90B0824, details[4]); // spazFactor
    PassToFirst(this, 0xEA6D0EFD, params);    // SetWobbleDetails
    FreeStruct(params);
}

// 0x0047E740
void CBlendChannel::SetLoopType(int loopType, bool send)
{
    if (send && m_id == 0) {
        if (GameNet::PlayerInfo* player = LocalPlayer(Owner(this)->m_id)) {
            Net::Client* app = ClientOf(player);
            uint8_t data[2] = {static_cast<uint8_t>(Owner(this)->m_id), static_cast<uint8_t>(loopType)};
            if (LastLoopType() != loopType) {
                Net::MsgDesc message;
                message.Construct();
                Send(app, message, kMsgLoopType, data, 2, false);
                LastLoopType() = static_cast<int8_t>(loopType);
            }
        }
    }
    if (CBlendTimeline* first = First(this))
        first->m_loopType = loopType;
}

// 0x0047E810
void CBlendChannel::Reverse(uint32_t /*unused*/)
{
    if (CBlendTimeline* first = First(this))
        first->m_direction = first->m_direction != 0 ? 0 : -1;
}

// 0x0047E850 (x87: the range and the ratio's products unrounded)
void CBlendChannel::SetSpeed(float speed, bool send, bool all, bool keepRatio)
{
    CBlendTimeline* first = First(this);
    if (!first)
        return;
    if (!all && std::abs(static_cast<double>(first->m_speed) * kFramesPerSecond - speed) < 0.05f) // (0x00645D6C)
        return;
    if (send && m_id == 0) {
        if (GameNet::PlayerInfo* player = LocalPlayer(Owner(this)->m_id)) {
            if (Net::Client* app = ClientOf(player)) {
                Net::MsgDesc message;
                message.Construct();
                struct {
                    float speed;
                    uint8_t object;
                } data;
                data.speed = speed;
                data.object = static_cast<uint8_t>(Owner(this)->m_id);
                Send(app, message, kMsgSpeed, &data, 8, false);
            }
        }
    }
    if (!all) {
        First(this)->m_speed = speed * kSecondsPerFrame;
        return;
    }
    if (keepRatio) {
        float start, current, end;
        First(this)->GetTimes(&start, &current, &end);
        float ratio = static_cast<float>(speed / (static_cast<double>(end) - start));
        for (Mem::ListNode* node = m_timelines.next; !IsHead(node) && node; node = node->next) {
            auto* timeline = reinterpret_cast<CBlendTimeline*>(node);
            timeline->GetTimes(&start, &current, &end);
            timeline->m_speed = static_cast<float>((static_cast<double>(end) - start) * ratio * kSecondsPerFrame);
        }
    } else {
        Mem::ListNode* node = m_timelines.next;
        if (IsHead(node) || !node)
            return;
        float perFrame = speed * kSecondsPerFrame;
        for (; !IsHead(node) && node; node = node->next)
            reinterpret_cast<CBlendTimeline*>(node)->m_speed = perFrame;
    }
}

// 0x0047EA40
double CBlendChannel::GetSpeed(CStruct* /*params*/, Script::CScript* /*script*/)
{
    CBlendTimeline* first = First(this);
    if (!first)
        return 0.0;
    return static_cast<double>(first->m_speed) * kFramesPerSecond;
}

// 0x0047EA80
void CBlendChannel::SetEventsOn(bool on)
{
    if (CBlendTimeline* first = First(this)) {
        m_eventsOn = on;
        first->m_eventsOn = on;
    }
}

// 0x0047F000
void CBlendChannel::SetAnimTags(uint32_t anim)
{
    CStruct* all = Script::GetStructure(0x95807202);
    CStruct* tags;
    if (!all || !all->GetStructure(anim, &tags, 0))
        return;
    CStruct* params = NewStruct();
    CStruct* copy = NewStruct();
    copy->AppendStructureIfAny(tags);
    params->AddStructurePointer(0x5DB4115F, copy); // AnimTags
    Owner(this)->SetTagsFromStruct(params);
    FreeStruct(params);
}

// 0x0047EB30
void __cdecl CBlendChannel::InitBuffers(int blends, int subsets)
{
    MaxBlends() = blends;
    Subsets() = subsets;
    Dbg::Printf(reinterpret_cast<const char*>(0x006486DC), reinterpret_cast<const char*>(0x006486FC), 0x963, 0);
    Mem::Manager* manager = Mem::Manager::Get();
    manager->InitNamedHeap(kAnimScratch, Subsets() * 0x840 + 0x90 + MaxBlends() * 0x848,
                           reinterpret_cast<const char*>(0x006486D0), 0); // "AnimScratch"
    manager->PushContext(manager->GetNamedHeap(kAnimScratch, 1));
    // (each pose's rotations made the identity by the vector constructor iterator 0x00401020 with 0x00464340; its
    // translations' constructor 0x005902E0 does nothing)
    auto newPoses = [](int count) {
        auto* poses = static_cast<CPose*>(Mem::New(static_cast<uint32_t>(count) * sizeof(CPose)));
        if (poses) {
            for (int i = 0; i < count; ++i) {
                for (auto& rotation : poses[i].rotations)
                    reinterpret_cast<Obj::IdentityQuat*>(rotation)->Set();
            }
        }
        return poses;
    };
    BlendBuffer() = newPoses(Subsets());
    Poses() = newPoses(MaxBlends());
    BlendSets() = static_cast<uint32_t*>(Mem::New(static_cast<uint32_t>(MaxBlends()) * 4));
    BlendWeights() = static_cast<float*>(Mem::New(static_cast<uint32_t>(MaxBlends()) * 4));
    Mem::Manager::Get()->PopContext();
}

} // namespace Gfx

REPLACE(0x0047DC40, &Gfx::CBlendChannel::SetFlipped);
REPLACE(0x0047DCF0, &Gfx::CBlendChannel::Finalize);
REPLACE(0x0047DD10, &Gfx::AnimListHead::CountItems);
REPLACE(0x0047DD40, &Gfx::AnimListNode::Destroy);
REPLACE(0x0047DD80, &Gfx::CBlendChannel::ToggleFlipped);
REPLACE(0x0047DDC0, &Gfx::CBlendChannel::RemoveDoneTimelines);
REPLACE(0x0047DE20, &Gfx::CBlendChannel::PushTimeline);
REPLACE(0x0047DF50, &Gfx::CBlendChannel::IsAnimOneOf);
REPLACE(0x0047E010, &Gfx::CBlendChannel::AddTime);
REPLACE(0x0047E030, &Gfx::CBlendChannel::UpdateBlend);
REPLACE(0x0047E1B0, &Gfx::CBlendChannel::UpdateTimelines);
REPLACE(0x0047E210, &Gfx::CBlendChannel::IsAnimComplete);
REPLACE(0x0047E240, &Gfx::CBlendChannel::IsCycling);
REPLACE(0x0047E280, &Gfx::CBlendChannel::SetWobbleTarget);
REPLACE(0x0047E450, &Gfx::CBlendChannel::SetWobbleDetails);
REPLACE(0x0047E740, &Gfx::CBlendChannel::SetLoopType);
REPLACE(0x0047E810, &Gfx::CBlendChannel::Reverse);
REPLACE(0x0047E850, &Gfx::CBlendChannel::SetSpeed);
REPLACE(0x0047EA40, &Gfx::CBlendChannel::GetSpeed);
REPLACE(0x0047EA80, &Gfx::CBlendChannel::SetEventsOn);
REPLACE(0x0047EAA0, &Gfx::CBlendChannel::GetPose);
REPLACE(0x0047EB30, Gfx::CBlendChannel::InitBuffers);
REPLACE(0x0047ED60, &Gfx::CBlendChannel::BlendPose);
REPLACE(0x0047EE90, &Gfx::CBlendChannel::DestroyBody);
REPLACE(0x0047EF20, &Gfx::CBlendChannel::Reset);
REPLACE(0x0047EF60, &Gfx::CBlendChannel::ReleaseWaitingScript);
REPLACE(0x0047EFB0, &Gfx::CBlendChannel::Update);
REPLACE(0x0047EFF0, &Gfx::CBlendChannel::BlendFirstPose);
REPLACE(0x0047F000, &Gfx::CBlendChannel::SetAnimTags);
REPLACE(0x0047F150, &Gfx::CBlendChannel::Construct);
REPLACE(0x0047F230, &Gfx::CBlendChannel::Destroy);
