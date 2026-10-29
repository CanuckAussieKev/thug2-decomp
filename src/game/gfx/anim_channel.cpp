// The animation blender's channel (Gfx::CBlendChannel, anim_blend.h): its timelines, blend value, wobble, speed, flip
// and loop messages, poses (0x0047DC40-0x0047F24E). From the disassembly.

#include "game/gfx/anim_blend.h"

#include <cmath>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/gfx/anim_track.h"
#include "game/gfx/frame_source.h"
#include "game/gfx/model_utils.h"
#include "game/gfx/skeleton.h"
#include "game/mth/math.h"
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

constexpr uint32_t kDefault = 0x1CA1FF20;           // Default
constexpr uint32_t kAnim = 0x98549BA4;              // anim
constexpr uint32_t kSpeed = 0xF0D90109;             // Speed
constexpr uint32_t kCurrent = 0x230CCBF4;           // Current
constexpr uint32_t kBackwards = 0xF8CFD515;         // Backwards
constexpr uint32_t kId = 0x40C698AF;                // id
constexpr uint32_t kSubsetId = 0x5CFCCF35;          // SubsetId
constexpr uint32_t kAnimTags = 0x5DB4115F;          // AnimTags
constexpr uint32_t kIndex = 0xD2D6A582;             // (a timeline's index)
constexpr uint32_t kPlayAnim = 0x0B1E7291, kObjPlayAnim = 0x2CB9AD09;
constexpr uint32_t kCycle = 0x4F792E6C, kPingPong = 0x3153E314, kHoldLoop = 0x6D941203; // the loop types' names
constexpr float kDefaultBlendPeriod = 0.3f;         // 0x3E99999A
constexpr float kFixedPoint = 4096.0f;              // 0x00648730

// The network messages a channel sends.
enum : uint8_t { kMsgPlayAnim = 0x29, kMsgWobbleTarget = 0x2B, kMsgFlip = 0x2C, kMsgWobbleDetails = 0x3C,
                 kMsgLoopType = 0x3D, kMsgSpeed = 0x41 };

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
CAnimTrack* Masked(Mem::ListNode* node) { return IsHead(node) ? nullptr : reinterpret_cast<CAnimTrack*>(node); }
Mem::ListNode* Node(CAnimTrack* timeline) { return reinterpret_cast<Mem::ListNode*>(timeline); }

// The playing timeline (the list's first), or null.
CAnimTrack* First(CBlendChannel* channel)
{
    Mem::ListNode* node = channel->m_timelines.next;
    return IsHead(node) || !node ? nullptr : reinterpret_cast<CAnimTrack*>(node);
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

// The track's fade (StartFade 0x00481B40), weight (0x00481B80, ST0) and operator new (0x00481EE0; the original
// passes it 0 too).
bool StartBlendOut(CAnimTrack* timeline, float period) { return timeline->StartFade(period); }
double Weight(CAnimTrack* timeline) { return timeline->GetWeight(); }
void* NewTimeline(uint32_t size) { return CAnimTrack::operator new(size); }

int CountListItems(Mem::ListNode* head) { return reinterpret_cast<AnimListHead*>(head)->CountItems(); }

// An animation's frame data in the anim set (Nx::GetAnimData 0x0048CF50: its first value the length), the range
// (GetAnimTimes 0x004877E0 - the original passes the script too, unused) and loop type (GetAnimLoopType 0x00487770)
// a play's parameters ask for, and a controller added to a track (0x00481FB0).
float* AnimLength(uint32_t key) { return static_cast<float*>(Nx::GetAnimData(key, 0)); }
void RangeFromParams(float* from, float* to, float current, float length, CStruct* params, Script::CScript* /*script*/)
{
    GetAnimTimes(from, to, current, length, params);
}
void LoopTypeFromParams(int* loopType, CStruct* params) { GetAnimLoopType(loopType, params); }
void AddController(CAnimTrack* timeline, CStruct* params) { timeline->AddController(params); }

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
    m_hasNextBlend = false;
    m_nextBlend = 0.0f;
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
    if (CAnimTrack* first = First(this)) {
        if (first->IsComplete())
            script->StopWaiting();
    }
}

// 0x0047E030 (x87: the blend's time left unrounded through the division)
void CBlendChannel::UpdateBlend()
{
    if (CAnimTrack* first = First(this))
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
    CAnimTrack* timeline = Masked(m_timelines.next);
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
        CAnimTrack* next = Masked(node->next);
        if (!reinterpret_cast<CAnimTrack*>(node)->IsActive()) {
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
    for (CAnimTrack* timeline = Masked(m_timelines.next); timeline;) {
        CAnimTrack* next = Masked(Node(timeline)->next);
        if (++count >= MaxBlends()) {
            Unlink(Node(timeline));
            DeleteTimeline(Node(timeline));
        }
        timeline = next;
    }
    if (CountListItems(&m_timelines) != 0) {
        CAnimTrack* first = First(this);
        if (first && StartBlendOut(first, period)) {
            float others = 0.0f;
            for (Mem::ListNode* node = Node(first)->next; !IsHead(node) && node; node = node->next) {
                auto* timeline = reinterpret_cast<CAnimTrack*>(node);
                if (timeline->IsFading())
                    others = static_cast<float>(static_cast<double>(timeline->m_fadeScale) * timeline->m_fadeLeft +
                                                others);
            }
            first->m_fadeScale = static_cast<float>((1.0 - others) / first->m_fadeLeft);
        }
    }
    void* memory = NewTimeline(sizeof(CAnimTrack));
    CAnimTrack* timeline =
        memory ? static_cast<CAnimTrack*>(memory)->Construct(Owner(this)) : nullptr;
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
    CAnimTrack* timeline = nullptr;
    Mem::ListNode* node = m_timelines.next;
    if (!IsHead(node) && node) {
        for (;;) {
            if (index-- == 0) {
                timeline = reinterpret_cast<CAnimTrack*>(node);
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
        reinterpret_cast<CAnimTrack*>(node)->AddTime(frames);
}

// 0x0047E210
bool CBlendChannel::IsAnimComplete()
{
    CAnimTrack* first = First(this);
    return first ? first->IsComplete() : false;
}

// 0x0047E240
bool CBlendChannel::IsCycling()
{
    CAnimTrack* first = First(this);
    if (!first)
        return false;
    return first->m_loopType == CAnimTimeline::kCycle || first->m_loopType == CAnimTimeline::kPingPong;
}

// 0x0047DF50
bool CBlendChannel::IsAnimOneOf(CStruct* params, Script::CScript* /*script*/)
{
    CAnimTrack* first = First(this);
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
    if (CAnimTrack* first = First(this))
        first->m_loopType = loopType;
}

// 0x0047E810
void CBlendChannel::Reverse(uint32_t /*unused*/)
{
    if (CAnimTrack* first = First(this))
        first->m_direction = first->m_direction != 0 ? 0 : -1;
}

// 0x0047E850 (x87: the range and the ratio's products unrounded)
void CBlendChannel::SetSpeed(float speed, bool send, bool all, bool keepRatio)
{
    CAnimTrack* first = First(this);
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
            auto* timeline = reinterpret_cast<CAnimTrack*>(node);
            timeline->GetTimes(&start, &current, &end);
            timeline->m_speed = static_cast<float>((static_cast<double>(end) - start) * ratio * kSecondsPerFrame);
        }
    } else {
        Mem::ListNode* node = m_timelines.next;
        if (IsHead(node) || !node)
            return;
        float perFrame = speed * kSecondsPerFrame;
        for (; !IsHead(node) && node; node = node->next)
            reinterpret_cast<CAnimTrack*>(node)->m_speed = perFrame;
    }
}

// 0x0047EA40
double CBlendChannel::GetSpeed(CStruct* /*params*/, Script::CScript* /*script*/)
{
    CAnimTrack* first = First(this);
    if (!first)
        return 0.0;
    return static_cast<double>(first->m_speed) * kFramesPerSecond;
}

// 0x0047EA80
void CBlendChannel::SetEventsOn(bool on)
{
    if (CAnimTrack* first = First(this)) {
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

// --- playing ---

// 0x0047F260
void CBlendChannel::PlayAnim(uint32_t anim, bool send, float from, float to, int loopType, float blendPeriod,
                             float speed, CStruct* params)
{
    if (!anim)
        anim = kDefault;
    if (send && m_id == 0) {
        if (GameNet::PlayerInfo* player = LocalPlayer(Owner(this)->m_id)) {
            Net::MsgDesc message;
            message.Construct();
            Net::Client* app = ClientOf(player);
            // loop type, object, anim, then the times, blend period and speed x 4096 as shorts
            uint8_t data[0xE];
            uint8_t object = static_cast<uint8_t>(Owner(this)->m_id);
            int16_t times[4] = {static_cast<int16_t>(Truncated(from * kFixedPoint)),
                                static_cast<int16_t>(Truncated(to * kFixedPoint)),
                                static_cast<int16_t>(Truncated(blendPeriod * kFixedPoint)),
                                static_cast<int16_t>(Truncated(speed * kFixedPoint))};
            data[0] = static_cast<uint8_t>(loopType);
            data[1] = object;
            std::memcpy(data + 2, &anim, 4);
            std::memcpy(data + 6, times, sizeof(times));
            Send(app, message, kMsgPlayAnim, data, sizeof(data), false);
        }
    }
    if (blendPeriod == 0.0f || !m_flag4C)
        reinterpret_cast<Obj::ListHead*>(&m_timelines)->DeleteAllNodes();
    PushTimeline(blendPeriod);
    Virtual<void>(Masked(m_timelines.next), 6, anim, from, to, loopType, blendPeriod, speed, (m_flags >> 29) & 1,
                  params);
    if (CStruct* tags = Owner(this)->mp_tags)
        tags->RemoveComponent(kAnimTags);
    SetAnimTags(anim);
}

// 0x004803D0
void CBlendChannel::PlayAnimation(uint32_t anim, float blendPeriod)
{
    PlayAnim(anim, false, 0.0f, *AnimLength(m_key + anim), 0, blendPeriod, 1.0f, nullptr);
}

// 0x00480410
void CBlendChannel::InitFromStructure(CStruct* params)
{
    params->GetChecksum(kSubsetId, &m_id, 0);
    params->GetFloat(0x4D786F62, &AsFloat(m_current), 0); // BlendWeight
    int priority = 0;
    params->GetInteger(0x3D836C96, &priority, 0);
    m_priority = priority;
    params->GetChecksum(0x5B8C6DC2, &m_eventTable, 0); // AnimEventTableName
    uint32_t animSet = 0; // (the original's is left as it was when neither is there)
    if (!params->GetChecksum(0x52A864B0, &animSet, 0))
        params->GetChecksum(0x09794932, &animSet, 1);
    m_key = animSet;
    PlayAnim(kDefault, false, 0.0f, 1000.0f, 0, kDefaultBlendPeriod, 1.0f, nullptr);
    Reset();
    uint32_t anim = kDefault;
    bool loop = params->GetChecksum(0xEAE64B47, &anim, 0);
    PlayAnim(anim, false, 0.0f, *AnimLength(m_key + anim), 0, kDefaultBlendPeriod, 1.0f, nullptr);
    if (loop)
        SetLoopType(CAnimTimeline::kCycle, true);
    if (params->ContainsFlag(0xCF466AA2))
        m_flag4C = true;
    SetEventsOn(params->ContainsFlag(0xE1B3F630));
}

// 0x0047F470 (x87: the synchronised start's fraction and product unrounded)
uint32_t CBlendChannel::PlayAnimFromScript(CStruct* params, Script::CScript* script, float blendPeriod)
{
    if (CAnimTrack* first = First(this))
        m_firstTime = first->m_current;
    if (m_flag4E) {
        CAnimTrack* first = First(this);
        if (!first || !first->IsComplete())
            return 0;
    }
    if (!m_key)
        return 0;
    uint32_t anim = 0;
    params->GetChecksum(kAnim, &anim, 0);
    if (!anim)
        Script::PrintContents(params, 0);
    float speed = 1.0f;
    params->GetFloat(kSpeed, &speed, 0);
    CAnimTrack* first = First(this);
    uint32_t playing = first ? first->m_anim : 0;
    if (anim == kCurrent) {
        anim = playing;
    } else if (playing == anim && params->ContainsFlag(0xFE09FE09)) { // NoRestart
        return 0;
    }
    if (!AnimLength(m_key + anim))
        anim = kDefault;

    float blend = blendPeriod;
    if (m_hasNextBlend) {
        blend = m_nextBlend;
        m_hasNextBlend = false;
    } else {
        float percent = 0.0f;
        if (params->GetFloat(0x4F3ABD6C, &percent, 0)) // (of the anim's length)
            blend = static_cast<float>(percent * static_cast<double>(0.01f) * *AnimLength(m_key + anim));
        else
            params->GetFloat(0x8F0D24ED, &blend, 0); // BlendPeriod
    }
    float length = *AnimLength(m_key + anim);
    float to = length;
    float from = 0.0f;
    float start = 0.0f; // the time the new one starts at
    bool sync = true;
    double fraction;
    if (params->ContainsFlag(0x099371AE)) { // SyncToPreviousAnim: as far through as the playing one
        float current, ends;
        reinterpret_cast<Obj::BlendChannelLinks*>(this)->GetTimes(&ends, &current, &ends);
        float playingLength = *AnimLength(m_key + playing);
        params->GetFloat(0x4B4A153A, &playingLength, 0); // (read and not used here)
        float through = static_cast<float>(current / *AnimLength(m_key + playing));
        if (through > 1.0f)
            fraction = 1.0;
        else if (through < 0.0f)
            fraction = 0.0;
        else
            fraction = through;
    } else if (params->ContainsFlag(0xFF5AC79E)) { // as far from the end as the playing one
        float current, ends;
        reinterpret_cast<Obj::BlendChannelLinks*>(this)->GetTimes(&ends, &current, &ends);
        float playingLength = *AnimLength(m_key + playing);
        params->GetFloat(0x4B4A153A, &playingLength, 0);
        fraction = Mth::Clamp(static_cast<float>((static_cast<double>(playingLength) - current) / playingLength),
                              0.0f, 1.0f);
    } else {
        sync = false;
        fraction = 0.0;
    }
    if (sync) {
        start = static_cast<float>(fraction * length);
        if (params->ContainsFlag(kBackwards))
            start = static_cast<float>(-(static_cast<double>(length) - start));
        speed = static_cast<float>(GetSpeed(nullptr, nullptr));
    }
    float current = 0.0f, ends = 0.0f;
    if (CAnimTrack* playingNow = First(this))
        playingNow->GetTimes(&ends, &current, &ends);
    RangeFromParams(&from, &to, current, length, params, script);
    int loopType = 0;
    LoopTypeFromParams(&loopType, params);
    m_flag4E = params->ContainsFlag(0x84F13067); // (wait for the playing one to complete)
    float duration = 0.0f;
    if (params->GetFloat(0x23272E4D, &duration, 0))
        speed = static_cast<float>(length / duration);
    PlayAnim(anim, true, from, to, loopType, blend, speed, params);
    CAnimTrack* timeline = Masked(m_timelines.next);
    if (start != 0.0f)
        timeline->AddTime(start);
    timeline->m_eventsOn = m_eventsOn;

    uint32_t partial;
    if (params->GetChecksum(0x6C565F57, &partial, 0)) { // PartialAnimOverlay: a partial animation controller too
        CStruct* controller = Script::NewStruct();
        if (controller)
            controller = controller->Init();
        uint32_t id = reinterpret_cast<uint32_t>(script); // (its argument's place reused: the id when there is one)
        if (params->GetChecksum(0xF0CE9E0F, &id, 0))
            controller->AddChecksum(kId, id);
        controller->AddChecksum(0x7321A8D6, 0x659BF355);  // Type PartialAnim
        controller->AddChecksum(0x6C2BFB7F, partial);     // AnimName
        controller->AddFloat(0x46E55E8F, from);           // From
        controller->AddFloat(0x28782D3B, to);             // To
        controller->AddChecksum(0u, 0xD029F619);           // Seconds
        controller->AddFloat(kCurrent, start);
        controller->AddFloat(kSpeed, speed);
        switch (loopType) {
        case CAnimTimeline::kCycle:
            controller->AddChecksum(0u, kCycle);
            break;
        case CAnimTimeline::kPingPong:
            controller->AddChecksum(0u, kPingPong);
            break;
        case CAnimTimeline::kHold:
            controller->AddChecksum(0u, kHoldLoop);
            break;
        default:
            break;
        }
        AddController(Masked(m_timelines.next), controller);
        FreeStruct(controller);
    }
    return anim;
}

// 0x0047FA00
int CBlendChannel::CallMemberFunction(uint32_t name, CStruct* params, Script::CScript* script)
{
    switch (name) {
    case 0x1DF59A54: { // the info of timeline Index (0xD2D6A582; the playing one without it) to the script
        int index;
        if (!params->GetInteger(kIndex, &index, 0)) {
            Mem::ListNode* node = m_timelines.next;
            if (!IsHead(node) && node)
                AddTimelineInfo(reinterpret_cast<CAnimTimeline*>(node), script->m_params);
            return 1;
        }
        for (CAnimTrack* timeline = Masked(m_timelines.next); timeline; timeline = Masked(Node(timeline)->next)) {
            if (index == 0) {
                AddTimelineInfo(timeline, script->m_params);
                break;
            }
            --index;
        }
        return 1;
    }
    case 0x27A98022: { // Flip: flipped, and every timeline's (or timeline Index's) controllers told
        int index;
        if (!params->GetInteger(kIndex, &index, 0)) {
            ToggleFlipped();
            for (Mem::ListNode* node = m_timelines.next; !IsHead(node) && node; node = node->next)
                Virtual<int>(node, 5, name, params, script);
            return 1;
        }
        if (index == 0)
            ToggleFlipped();
        for (CAnimTrack* timeline = Masked(m_timelines.next); timeline; timeline = Masked(Node(timeline)->next)) {
            if (index == 0) {
                Virtual<int>(timeline, 5, name, params, script);
                break;
            }
            --index;
        }
        return 1;
    }
    case 0x2889C6C9:
    case 0x76CC99D5: // (whether the animation is complete)
        return IsAnimComplete() ? 1 : 0;
    case kPlayAnim:
    case kObjPlayAnim: {
        PlayAnimFromScript(params, script, kDefaultBlendPeriod);
        Mem::ListNode* node = m_timelines.next;
        if (!IsHead(node) && node && reinterpret_cast<CAnimTrack*>(node)->m_loopType == CAnimTimeline::kHold)
            Virtual<int>(mp_owner, 3, 0xEA6D0EFDu, params, script); // (the object's own member function)
        return 1;
    }
    case 0x42FFC3CE: { // GetAnimLength: of anim, to the script's length
        uint32_t anim = 0;
        params->GetChecksum(kAnim, &anim, 0);
        if (HasValue(anim))
            script->m_params->AddFloat(0xFE82614D, std::fabs(GetValue(anim))); // length
        return 1;
    }
    case 0x4FAF61A9: { // every timeline's speed so its range takes 0x23272E4D seconds
        float duration = 0.0f;
        params->GetFloat(0x23272E4D, &duration, 1);
        for (Mem::ListNode* node = m_timelines.next; !IsHead(node) && node; node = node->next) {
            auto* timeline = reinterpret_cast<CAnimTrack*>(node);
            float start, current, end;
            timeline->GetTimes(&start, &current, &end);
            float larger = start > end ? start : end;
            timeline->m_speed = static_cast<float>(larger / static_cast<double>(duration) * kSecondsPerFrame);
        }
        return 1;
    }
    case 0x58C52F64: // the loop type: once with 0xD443A2BC, else cycling
        SetLoopType(params->ContainsFlag(0xD443A2BC) ? CAnimTimeline::kOnce : CAnimTimeline::kCycle, true);
        return 1;
    case 0x5195BCBB: // the speed to the script's Speed
        script->m_params->AddFloat(kSpeed, static_cast<float>(GetSpeed(params, script)));
        return 1;
    case 0x68C86AEC: // the next play's blend period
        params->GetFloat(0u, &m_nextBlend, 0);
        m_hasNextBlend = true;
        return 1;
    case 0x6ECEB234: // whether it's flipped
        return (m_flags >> 29) & 1;
    case 0x74CF5C94: // WaitAnim
        return WaitAnim(params);
    case 0x80FDBDF2: // whether it cycles
        return IsCycling() ? 1 : 0;
    case 0x9069F357: { // AnimExists (the anim set's unnamed checksum)
        uint32_t anim = reinterpret_cast<uint32_t>(params); // (its argument's place reused, as the original)
        params->GetChecksum(0u, &anim, 0);
        return HasValue(anim) ? 1 : 0;
    }
    case 0x922E7D14: // whether the unnamed time (frames) was passed since the last frame
        return TimePassed(params);
    case 0x986D274E: // to every timeline
        for (Mem::ListNode* node = m_timelines.next; !IsHead(node) && node; node = node->next)
            Virtual<int>(node, 5, name, params, script);
        return 1;
    case 0x98A32669: { // flag4C from enabled
        int enabled = static_cast<int>(reinterpret_cast<uintptr_t>(params)); // (the argument's place, as above)
        params->GetInteger(0xAF06447B, &enabled, 1);
        m_flag4C = enabled != 0;
        return 1;
    }
    case 0x1A0F9646: // AnimEquals
    case 0xB5FB7EB6:
        return IsAnimOneOf(params, script) ? 1 : 0;
    case 0xC68365EB: // anim (Default when there's none) to timeline Index's (the playing one's) controllers
        return PassAnimToTimeline(params, script);
    case 0xEA6D0EFD:
        return 1;
    case 0x5E3F0F17:
    case 0x6AAEB76F:
    case 0x83654874:
    case 0xBD4EDD44: // SetPartialAnimSpeed
    case 0xD63A1B81: // GetPartialAnimParams
    case 0xF5E2B871: {
        Mem::ListNode* node = m_timelines.next;
        if (IsHead(node) || !node)
            return 1;
        return Virtual<int>(node, 5, name, params, script) != 0 ? 1 : 0;
    }
    default:
        return 2;
    }
}

// The WaitAnim member function (0x0047FDCE): the script waits until the playing timeline reaches a time - in
// frames (Frame / Frames), a percentage of its range (Percent), seconds (0x49E0EE96 / Seconds) or milliseconds -
// from the current time (0x91A4C826), the end (FromEnd) or its start; the object's script made if it has none.
int CBlendChannel::WaitAnim(CStruct* params)
{
    float start, current, end;
    reinterpret_cast<Obj::BlendChannelLinks*>(this)->GetTimes(&start, &current, &end);
    float time = 0.0f;
    params->GetFloat(0u, &time, 0);
    if (params->ContainsFlag(0x4A07C332) || params->ContainsFlag(0x019176C5)) // Frame, Frames
        time = time * kSecondsPerFrame;
    else if (params->ContainsFlag(0x9E497FC6)) // Percent
        time = static_cast<float>((static_cast<double>(end) - start) * time * static_cast<double>(0.01f) + start);
    else if (!params->ContainsFlag(0x49E0EE96) && !params->ContainsFlag(0xD029F619)) // (Seconds: as it is)
        time = time * 0.001f;
    bool forwards = start <= end;
    // (x87: rounded once to a float; a NaN kept as it is - not negated)
    auto sum = [](float a, float b) { return static_cast<float>(static_cast<double>(a) + b); };
    auto difference = [](float a, float b) { return static_cast<float>(static_cast<double>(a) - b); };
    if (params->ContainsFlag(0x91A4C826))
        m_waitTime = forwards ? sum(time, current) : difference(current, time);
    else if (params->ContainsFlag(0xE18CD075)) // FromEnd
        m_waitTime = forwards ? difference(end, time) : sum(time, start);
    else
        m_waitTime = time;
    if (forwards ? m_waitTime > end : m_waitTime < end)
        m_waitTime = end;
    Obj::CCompositeObject* owner = Owner(this);
    if (!owner->m_script) {
        void* memory = Script::CScript::operator new(sizeof(Script::CScript));
        owner->m_script = memory ? static_cast<Script::CScript*>(memory)->Construct() : nullptr;
    }
    if ((start <= end && current >= m_waitTime) || (start >= end && current <= m_waitTime)) {
        owner->m_script->StopWaiting(); // (already there)
        m_waiting = false;
    } else {
        owner->m_script->Block(0);
        m_waiting = true;
    }
    return 1;
}

// The member function 0x922E7D14 (0x004800D3): whether the time given (the unnamed float, in frames, from the
// start) lies between the playing timeline's time last frame and now - either way round.
int CBlendChannel::TimePassed(CStruct* params)
{
    float start, current, end;
    reinterpret_cast<Obj::BlendChannelLinks*>(this)->GetTimes(&start, &current, &end);
    float time = 0.0f;
    params->GetFloat(0u, &time, 0);
    time = time * kSecondsPerFrame;
    float now = static_cast<float>(static_cast<double>(current) - start);
    float before = static_cast<float>(static_cast<double>(m_firstTime) - start);
    if (now == time)
        return 1;
    if (before < time && time < now)
        return 1;
    if (now < before && before < time)
        return 1;
    if (time < now && now < before)
        return 1;
    return 0;
}

// The member function 0xC68365EB (0x00480212): anim (Default added when the anim set hasn't it) and its length
// (Duration) to timeline Index's controllers (the playing one's without it - its tags then set from the anim). Their
// answer (as true / false), or false without such a timeline.
int CBlendChannel::PassAnimToTimeline(CStruct* params, Script::CScript* script)
{
    uint32_t anim = reinterpret_cast<uint32_t>(params); // (the argument's place, as above)
    params->GetChecksum(kAnim, &anim, 0);
    if (!HasValue(anim)) {
        anim = kDefault;
        params->AddChecksum(kAnim, kDefault);
    }
    params->AddFloat(0x79A07F3F, GetValue(anim)); // Duration
    int index;
    CAnimTrack* timeline = Masked(m_timelines.next);
    if (params->GetInteger(kIndex, &index, 0)) {
        for (; timeline && index; --index)
            timeline = Masked(Node(timeline)->next);
    }
    if (!timeline)
        return 0;
    if (timeline == Masked(m_timelines.next)) {
        ClearFlag();
        SetAnimTags(anim);
    }
    return Virtual<int>(timeline, 5, 0xC68365EBu, params, script) != 0 ? 1 : 0;
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
REPLACE(0x0047F260, &Gfx::CBlendChannel::PlayAnim);
REPLACE(0x0047F470, &Gfx::CBlendChannel::PlayAnimFromScript);
REPLACE(0x0047FA00, &Gfx::CBlendChannel::CallMemberFunction);
REPLACE(0x004803D0, &Gfx::CBlendChannel::PlayAnimation);
REPLACE(0x00480410, &Gfx::CBlendChannel::InitFromStructure);
