// The animation blender component and the pieces of its channels' links kept with it.

#include "engine/objects/anim_blender.h"

#include <bit>
#include <cstdint>
#include <gsl/pointers>
#include <array>
#include <span>
#include <gsl/assert>

#include "engine/core/checked_index.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/anim_blend.h"
#include "engine/gfx/anim_controller.h"
#include "engine/gfx/rig.h"
#include "engine/net/buddy_status.h"
#include "engine/objects/movie_manager.h"
#include "engine/objects/pager_message.h"
#include "engine/objects/suspend_component.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/lists/chain_start.h"
#include "game/rider/thaw_bmx.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/flexible_particle.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/skeleton_component.h"
#include "engine/objects/model_component.h"
#include "engine/gfx/model.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/down_cast.h"

namespace objects {
namespace {
constexpr unsigned kByteMask = 0xFF;
constexpr int kAllChannelsFlagBit = 29;
constexpr uint32_t kObjAddAnimSubset = 0xCABB44AE; // Obj_AddAnimSubset
constexpr uint32_t kAnimBlender = 0x7149B753; // AnimBlender
constexpr uint32_t kOBJWaitanimfinished = 0xB628A959; // OBJ_waitanimfinished
constexpr uint32_t kObjRemoveAnimSubset = 0xAFC2493E; // Obj_RemoveAnimSubset
constexpr uint32_t kObjSetAnimSubsetWeight = 0xF1D4B29F; // Obj_SetAnimSubsetWeight
constexpr uint32_t kObjAnimSubsetExists = 0xD58E2AD5; // Obj_AnimSubsetExists
constexpr uint32_t kOBJEnableAnimBlending = 0x98A32669; // OBJ_EnableAnimBlending
constexpr uint32_t kObjSetAnimCycleMode = 0x58C52F64; // Obj_SetAnimCycleMode
constexpr uint32_t kObjPlayAnimName = 0x2CB9AD09; // Obj_PlayAnim
constexpr uint32_t kObjAnimComplete = 0x2889C6C9; // Obj_AnimComplete
constexpr uint32_t kAnimFinished = 0x76CC99D5; // AnimFinished
constexpr uint32_t kObjAnimEquals = 0xB5FB7EB6; // Obj_AnimEquals
constexpr uint32_t kAnimEquals = 0x1A0F9646; // AnimEquals
constexpr uint32_t kObjGetAnimSpeed = 0x5195BCBB; // Obj_GetAnimSpeed
constexpr uint32_t kBlendPeriodOut = 0x68C86AEC; // BlendPeriodOut
constexpr uint32_t kLoopingAnim = 0x80FDBDF2; // LoopingAnim
constexpr uint32_t kGetAnimLength = 0x42FFC3CE; // GetAnimLength
constexpr uint32_t kFrameIs = 0x922E7D14; // FrameIs
constexpr uint32_t kWaitAnim = 0x74CF5C94; // WaitAnim
constexpr uint32_t kAnimExists = 0x9069F357; // AnimExists
constexpr uint32_t kTimelineInfo = 0x1DF59A54; // text unknown (the info of a timeline to the script)
constexpr uint32_t kReplaceAnim = 0xC68365EB; // ReplaceAnim
constexpr uint32_t kAddAnimController = 0x83654874; // AddAnimController
constexpr uint32_t kGetPartialAnimParams = 0xD63A1B81; // GetPartialAnimParams
constexpr uint32_t kSetPartialAnimSpeed = 0xBD4EDD44; // SetPartialAnimSpeed
constexpr uint32_t kStepFrames = 0x6AAEB76F; // text unknown (a step of frames)
constexpr uint32_t kReversePartialAnimDirection = 0xF5E2B871; // ReversePartialAnimDirection
constexpr uint32_t kRemoveAnimController = 0x986D274E; // RemoveAnimController
constexpr uint32_t kObjFlip = 0x27A98022; // Obj_Flip
constexpr uint32_t kObjAnimationFlipped = 0x6ECEB234; // Obj_AnimationFlipped
constexpr uint32_t kSkeleton = 0x222756D5; // Skeleton
constexpr uint32_t kModel = 0x286A8D26; // Model
constexpr uint32_t kSuspend = 0xCE0CA665; // Suspend
constexpr uint32_t kSetWobbleDetails = 0xEA6D0EFD; // SetWobbleDetails
constexpr uint32_t kReferenceChecksum = 0x52A864B0; // ReferenceChecksum
constexpr uint32_t kBlendWeight = 0x4D786F62; // BlendWeight
constexpr uint32_t kBlendDuration = 0xBF237659; // BlendDuration
constexpr uint32_t kSubsetExists = 0xC7C6228C; // subsetexists (letter case unknown)
} // namespace

std::array<script::MemberFunctionEntry, kAnimBlenderMemberFunctionCount>& anim_blender_member_functions()
{
    static std::array<script::MemberFunctionEntry, kAnimBlenderMemberFunctionCount> anim_blender_member_functions{{
        {.callsign_ = kObjAddAnimSubset, .owner_kind_ = kAnimBlender}, {.callsign_ = kOBJWaitanimfinished, .owner_kind_ = kAnimBlender}, {.callsign_ = kObjRemoveAnimSubset, .owner_kind_ = kAnimBlender}, {.callsign_ = kObjSetAnimSubsetWeight, .owner_kind_ = kAnimBlender},
        {.callsign_ = kObjAnimSubsetExists, .owner_kind_ = kAnimBlender}, {.callsign_ = kOBJEnableAnimBlending, .owner_kind_ = kAnimBlender}, {.callsign_ = kObjSetAnimCycleMode, .owner_kind_ = kAnimBlender}, {.callsign_ = kObjPlayAnimName, .owner_kind_ = kAnimBlender},
        {.callsign_ = kObjAnimComplete, .owner_kind_ = kAnimBlender}, {.callsign_ = kAnimFinished, .owner_kind_ = kAnimBlender}, {.callsign_ = kObjAnimEquals, .owner_kind_ = kAnimBlender}, {.callsign_ = kAnimEquals, .owner_kind_ = kAnimBlender},
        {.callsign_ = kObjGetAnimSpeed, .owner_kind_ = kAnimBlender}, {.callsign_ = kBlendPeriodOut, .owner_kind_ = kAnimBlender}, {.callsign_ = kLoopingAnim, .owner_kind_ = kAnimBlender}, {.callsign_ = kGetAnimLength, .owner_kind_ = kAnimBlender},
        {.callsign_ = kFrameIs, .owner_kind_ = kAnimBlender}, {.callsign_ = kWaitAnim, .owner_kind_ = kAnimBlender}, {.callsign_ = kAnimExists, .owner_kind_ = kAnimBlender}, {.callsign_ = kTimelineInfo, .owner_kind_ = kAnimBlender},
        {.callsign_ = kReplaceAnim, .owner_kind_ = kAnimBlender}, {.callsign_ = kAddAnimController, .owner_kind_ = kAnimBlender}, {.callsign_ = kGetPartialAnimParams, .owner_kind_ = kAnimBlender}, {.callsign_ = kSetPartialAnimSpeed, .owner_kind_ = kAnimBlender},
        {.callsign_ = kStepFrames, .owner_kind_ = kAnimBlender}, {.callsign_ = kReversePartialAnimDirection, .owner_kind_ = kAnimBlender}, {.callsign_ = kRemoveAnimController, .owner_kind_ = kAnimBlender}, {.callsign_ = kObjFlip, .owner_kind_ = kAnimBlender},
        {.callsign_ = kObjAnimationFlipped, .owner_kind_ = kAnimBlender}, {.callsign_ = 0, .owner_kind_ = kAnimBlender}}};
    return anim_blender_member_functions;
}

namespace {

constexpr uint32_t kId = 0x5CFCCF35; // SubsetId
constexpr uint32_t kPlayAnim = 0x0B1E7291, kObjPlayAnim = 0x2CB9AD09;
constexpr float kMinWeight = 0.00001f;

bool is_head(const memory::ListNode* node) { return node->data() == memory::ListNode::head_marker(); }

using Channel = gfx::BlendChannel;
// A channel's list node; none for no channel (the original took its address through a null channel: null).
memory::ListNode* get_link(Channel* channel) { return channel != nullptr ? &channel->list_link() : nullptr; }
// (a channel's node's data is the channel - the original took the node's address as it)
Channel* as_channel(memory::ListNode* node) { return static_cast<Channel*>(node->data()); }
// A node as the original masks it: null when it's the head.
Channel* masked(memory::ListNode* node) { return is_head(node) ? nullptr : as_channel(node); }
// The next channel after `channel` (or the head).
memory::ListNode* get_next(Channel* channel) { return get_link(channel)->next(); }
// The walks' end test: the head or null.
bool is_over(memory::ListNode* node) { return is_head(node) || !node; }

uint32_t& ident_no(Channel* channel)
{
    Expects(channel != nullptr);
    return channel->ident();
}

// A bool read from a 32-bit argument slot as the callee does: its low byte (the callers pass raw words).
bool bool_arg(uint32_t word) { return (word & kByteMask) != 0; }
bool bool_arg(float word) { return bool_arg(std::bit_cast<uint32_t>(word)); }

} // namespace

// 0x0041A5B0
gsl::owner<ObjectModule*> AnimBlenderComponent::fabricate() { return new AnimBlenderComponent; }

// 0x00419690
void AnimBlenderComponent::publish_method_table()
{
    script::register_member_function_table(anim_blender_member_functions());
}

// 0x0041A410
AnimBlenderComponent::AnimBlenderComponent()
{
    lanes_.make_head();
    observers_.make();
    set_kind_tag(kType);
}

// 0x0041A4C0 (0x0041A610 the deleting one)
AnimBlenderComponent::~AnimBlenderComponent()
{
    lists::delete_each<Channel>(&lanes_, [](Channel* channel) { channel->teardown(1); }); // (0x0057A2F0)
    for (const auto* node = observers_.first(); node != observers_.head(); node = node->next) {
        gsl::owner<decltype(node->value)> observer = memory::owned_element(node->value); // (the list's)
        delete observer;
    }
    // the list emptied (std::list::erase(begin, end), 0x004ECC30) - twice, as the original
    for (int pass = 0; pass < 2; ++pass)
        observers_.erase(observers_.first(), observers_.head());
    memory::Hub::get_pull()->dealloc(observers_.head());
    lanes_.uncouple(); // (0x00419D70)
}

// 0x0041A260 (the 0xCE0CA665 component's answer goes to each channel as a dword whose high byte, as in the
// original, may not be clear - only its low byte is meant)
void AnimBlenderComponent::tick_frame()
{
    uint32_t highest_set = 0;
    int count = 0;
    bool changed = false;
    Channel* channel = masked(lanes_.next());
    const bool ready = core::down_cast<DormancyModule>(component18_)->wants_animating(&distance_squared_);
    if (!channel)
        return;
    for (;;) {
        if (!(channel->ongoing() >= kMinWeight) && count != 0) {
            // a channel after the first with (next to) no weight: only reset
            channel->update_blend();
            channel->update_timelines();
            core::at(gfx::blend_sets(), count) = static_cast<uint32_t>(channel->rank());
            if (!(highest_set > core::at(gfx::blend_sets(), count)))
                highest_set = core::at(gfx::blend_sets(), count);
            core::at(gfx::blend_weights(), count) = 0.0f;
        } else {
            if (channel->tick_frame(count, ready))
                changed = true;
            core::at(gfx::blend_weights(), count) = channel->ongoing();
            core::at(gfx::blend_sets(), count) = static_cast<uint32_t>(channel->rank());
            if (!(highest_set > core::at(gfx::blend_sets(), count)))
                highest_set = core::at(gfx::blend_sets(), count);
        }
        memory::ListNode* next = get_next(channel);
        ++count;
        if (is_over(next))
            break;
        channel = as_channel(next);
    }
    if (!changed)
        return;
    // (the skeleton component's +0x18, through the getter folded at 0x005480F0)
    auto skeleton = [this]() {
        return (core::down_cast<objects::BoneRigModule>(bone_rig_)->bone_rig());
    };
    if (count == 1) {
        notify_observers(gfx::blend_buffer().data());
    } else {
        const uint32_t sets = highest_set + 1;
        for (uint32_t set = 0; set != sets;) {
            gfx::Stance& pose = core::at(gfx::blend_buffer(), 0);
            auto bones = static_cast<uint32_t>(skeleton()->get_num_bones());
            gfx::blend_poses_by_id(gfx::blend_buffer(), gfx::blend_weights(), gfx::blend_sets(),
                                   static_cast<int>(bones), pose, count, checksums::Name{set});
            ++set;
            core::at(gfx::blend_sets(), 0) = set;
            core::at(gfx::blend_weights(), 0) = 0.0f;
            if (!(set < sets))
                break;
        }
        notify_observers(gfx::blend_buffer().data());
    }
    skeleton()->tick_frame(gfx::blend_buffer().data());
}

// 0x00419DD0
void AnimBlenderComponent::configure_from_data(script::Struct* params)
{
    if (lanes_.prev() != &lanes_)
        return;
    void* memory = memory::alloc_zeroed(sizeof(Channel));
    Channel* channel = memory ? memory::construct_in<gfx::BlendChannel>(memory, entity(), this, true) : nullptr;
    get_link(channel)->set_prev(&lanes_);
    get_link(channel)->set_next(lanes_.next());
    lanes_.next()->set_prev(get_link(channel));
    lanes_.set_next(get_link(channel));
    memory::enter_heap_scope(memory::bottom_up_heap());
    memory::ListNode* first = lanes_.next();
    if (!is_over(first)) {
        as_channel(first)->configure_from_data(params);
        as_channel(first)->set_events_on(true);
    }
    memory::leave_heap_scope();
}

// 0x004196D0
void AnimBlenderComponent::seal()
{
    for (memory::ListNode* node = lanes_.next(); !is_over(node); node = node->next())
        as_channel(node)->seal();
    bone_rig_ = entity()->fetch_module(kSkeleton);
    mesh_instance_ = entity()->fetch_module(kModel);
    component18_ = entity()->fetch_module(kSuspend);
}

// 0x00419980
void AnimBlenderComponent::step_wait_state(script::Script* script, uint32_t id)
{
    for (memory::ListNode* node = lanes_.next(); !is_over(node); node = node->next()) {
        if (ident_no(as_channel(node)) == id)
            as_channel(node)->release_waiting_script(script);
    }
}

// 0x00419950
int AnimBlenderComponent::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    if (name != kPlayAnim && name != kSetWobbleDetails)
        return 2;
    return pass_to_channel(name, params, script);
}

// 0x0041A1A0 (no channel to take the anim set from: a crash, as in the original)
int AnimBlenderComponent::add_channel_from_script(uint32_t, script::Struct* params, script::Script*)
{
    uint32_t id = 0;
    const Channel* first = masked(lanes_.next());
    params->read_hash_value(kId, &id, 0);
    params->put_name_key(checksums::Name{kReferenceChecksum}, first->lookup_key());
    const bool at_front = params->has_bare_flag(0x580C0963);
    Channel* channel = add_channel(id, at_front);
    channel->configure_from_data(params);
    channel->seal();
    return 1;
}

// 0x00419E90 (the channel with the id is looked for but the result isn't used; no id and no channel: a crash)
int AnimBlenderComponent::wait_on_channel(uint32_t, script::Struct* params, script::Script* script)
{
    uint32_t id = 0;
    Channel* first = masked(lanes_.next());
    uint32_t wait_for = 0;
    if (params && (params->read_hash_value(kId, &id, 0), id != 0))
        wait_for = id;
    else
        wait_for = ident_no(first);
    script->set_wait_on_component(script::BlockReason::kAnimDone, this, wait_for);
    return 1;
}

// 0x00419750
int AnimBlenderComponent::remove_channel(uint32_t, script::Struct* params, script::Script*)
{
    uint32_t id = 0;
    Channel* channel = masked(lanes_.next());
    params->read_hash_value(kId, &id, 0);
    if (!channel)
        return 1;
    while (ident_no(channel) != id) {
        memory::ListNode* next = get_next(channel);
        if (is_over(next))
            return 1;
        channel = as_channel(next);
    }
    channel->teardown(1);
    return 1;
}

// 0x004197C0 (no id and no channel: blend_to on null, a crash, as in the original)
int AnimBlenderComponent::blend_channel(uint32_t, script::Struct* params, script::Script*)
{
    uint32_t id = 0;
    Channel* channel = masked(lanes_.next());
    params->read_hash_value(kId, &id, 0);
    if (id != 0) {
        if (!channel)
            return 1;
        while (ident_no(channel) != id) {
            memory::ListNode* next = get_next(channel);
            if (is_over(next))
                return 1;
            channel = as_channel(next);
        }
    }
    float a = 0.0f, b = 0.0f;
    params->fetch_float(kBlendWeight, &a, 1);
    params->fetch_float(kBlendDuration, &b, 0);
    channel->blend_to({.target = a, .period = b});
    return 1;
}

// 0x00419F10 (not a Ghidra function)
int AnimBlenderComponent::channel_exists(uint32_t, script::Struct* params, script::Script* script)
{
    uint32_t id = 0;
    params->read_hash_value(kId, &id, 0);
    for (memory::ListNode* node = lanes_.next(); !is_over(node); node = node->next()) {
        if (ident_no(as_channel(node)) == id) {
            script->args()->put_name_key(checksums::Name{0u}, kSubsetExists); // (an unnamed flag)
            return 1;
        }
    }
    return 0;
}

// 0x004198D0 (no id given and no channel: a call on null, a crash, as in the original)
int AnimBlenderComponent::pass_to_channel(uint32_t name, script::Struct* params, script::Script* script)
{
    uint32_t id = 0;
    Channel* channel = masked(lanes_.next());
    if (params && (params->read_hash_value(kId, &id, 0), id != 0)) {
        if (!channel)
            return 0;
        while (ident_no(channel) != id) {
            memory::ListNode* next = get_next(channel);
            if (is_over(next))
                return 0;
            channel = as_channel(next);
        }
    }
    // NOT ORIGINAL - THAW's BMX ([features] thaw_bmx; the local rider on the bike and his bike): THAW's animation tree
    // flips a branch as it's played (Skater_Playanim's obj_is_flipped), so the channel takes the rider's flipped flag
    // before it plays - on the bike his Flip turns the flag only (game/rider/thaw_bmx.h play_flip). 0x0047DC40 with
    // its last argument 0: the flag set, nothing sent. Original behaviour: the channel's flip as it was.
    if ((name == kPlayAnim || name == kObjPlayAnim) && channel && skater::thaw_bmx::is_switched_on()) {
        const int flip = skater::thaw_bmx::play_flip(entity());
        if (flip >= 0)
            channel->set_flipped(entity()->ident(), bool_arg(static_cast<uint32_t>(flip)), 0u, bool_arg(0u));
    }
    Expects(channel != nullptr);
    return channel->invoke_method_by_name(name, params, script);
}

// 0x00419D40
gfx::BlendChannel* AnimBlenderComponent::find_channel(uint32_t id)
{
    for (memory::ListNode* node = lanes_.next(); !is_over(node); node = node->next()) {
        if (ident_no(as_channel(node)) == id)
            return as_channel(node);
    }
    return nullptr;
}

// 0x00419FD0 (the original first looks for a channel with the id and drops the result)
gfx::BlendChannel* AnimBlenderComponent::add_channel(uint32_t id, bool at_front)
{
    void* memory = memory::alloc_zeroed(sizeof(Channel));
    Channel* channel = memory ? memory::construct_in<gfx::BlendChannel>(memory, entity(), this, false) : nullptr;
    ident_no(channel) = id;
    channel->seal();
    memory::ListNode* link = get_link(channel);
    if (at_front) {
        link->set_prev(&lanes_);
        link->set_next(lanes_.next());
        lanes_.next()->set_prev(link);
        lanes_.set_next(link);
    } else {
        link->set_prev(lanes_.prev());
        link->set_next(&lanes_);
        lanes_.prev()->set_next(link);
        lanes_.set_prev(link);
    }
    return channel;
}

// 0x0041A220
void AnimBlenderComponent::notify_observers(gfx::Stance* pose)
{
    for (const auto* node = observers_.first(); node != observers_.head(); node = node->next)
        node->value->on_pose(pose);
}

// 0x004199C0
void AnimBlenderComponent::visit_channels()
{
    for (memory::ListNode* node = lanes_.next(); !is_over(node); node = node->next()) {
        // (0x005480A0: an empty function)
    }
}

// 0x004199F0
void AnimBlenderComponent::pass_to_each_channel(uint32_t value)
{
    for (memory::ListNode* node = lanes_.next(); !is_over(node); node = node->next())
        as_channel(node)->advance_clock(std::bit_cast<float>(value));
}

// 0x00419A20 (with no channel, the original reads the head's next from null: a crash)
void AnimBlenderComponent::reset_channels()
{
    for (memory::ListNode* node = lanes_.next(); !is_over(node); node = node->next())
        as_channel(node)->get_reset();
    memory::ListNode* node = get_link(masked(lanes_.next()))->next();
    if (is_over(node))
        return;
    while (node) {
        memory::ListNode* next = node->next();
        memory::ListNode* following = is_head(next) ? nullptr : next;
        as_channel(node)->teardown(1);
        node = following;
    }
}

// 0x00419C10
bool AnimBlenderComponent::any_channel(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    bool any = false;
    for (memory::ListNode* node = lanes_.next(); !is_over(node); node = node->next()) {
        if (as_channel(node)->set_flipped(a, bool_arg(b), c, bool_arg(d)))
            any = true;
    }
    return any;
}

// 0x00419C70
bool AnimBlenderComponent::all_channels_flag29()
{
    bool all = true;
    for (memory::ListNode* node = lanes_.next(); !is_over(node); node = node->next())
        all = all && ((as_channel(node)->flag_bits() >> kAllChannelsFlagBit) & 1);
    return all;
}

// 0x00419CA0
bool AnimBlenderComponent::model_ready()
{
    const render::RenderModel* thing = core::down_cast<MeshInstanceModule>(mesh_instance_)->mesh_instance().get();
    if (!thing)
        return false;
    const uint8_t flags = thing->flag_bits();
    return (flags & 2) && !(flags & 4);
}

// 0x00419730
void AnimBlenderComponent::first_channel_47e_ff0()
{
    if (Channel* channel = masked(lanes_.next()))
        channel->blend_first_pose();
}

// 0x00419A90
void AnimBlenderComponent::play_animation(uint32_t anim, uint32_t b)
{
    if (Channel* channel = masked(lanes_.next()))
        channel->play_animation(anim, std::bit_cast<float>(b));
}

// 0x00419B50
void AnimBlenderComponent::play_animation_ex(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f,
                                             uint32_t g)
{
    if (Channel* channel = masked(lanes_.next()))
        channel->play_anim(a, bool_arg(b), std::bit_cast<float>(c), std::bit_cast<float>(d), static_cast<int>(e),
                           std::bit_cast<float>(f), std::bit_cast<float>(g), nullptr);
}

// 0x00419B90
void AnimBlenderComponent::first_channel_47e_850(float a, float b, float c, float d)
{
    if (Channel* channel = masked(lanes_.next()))
        channel->set_speed(a, bool_arg(b), bool_arg(c), bool_arg(d));
}

// 0x00419BB0
void AnimBlenderComponent::first_channel_47e_810(uint32_t a)
{
    if (Channel* channel = masked(lanes_.next()))
        channel->get_reverse(a);
}

// 0x00419BD0
void AnimBlenderComponent::first_channel_47e_280(float a, uint32_t b)
{
    if (Channel* channel = masked(lanes_.next()))
        channel->choose_wobble_goal(a, bool_arg(b));
}

// 0x00419BF0
void AnimBlenderComponent::first_channel_47e_450(std::span<const float, kBlendParamCount> a, uint32_t b)
{
    if (Channel* channel = masked(lanes_.next()))
        channel->set_wobble_details(a, bool_arg(b));
}

// 0x00419B30
void AnimBlenderComponent::set_flag_4c(bool value)
{
    if (Channel* channel = masked(lanes_.next()))
        channel->set_flag_4c(value);
}

// 0x00419AB0
uint32_t AnimBlenderComponent::get_anim_set()
{
    const Channel* channel = masked(lanes_.next());
    return channel ? channel->lookup_key() : 0;
}

// 0x00419AD0
uint32_t AnimBlenderComponent::get_first_channel58()
{
    const Channel* channel = masked(lanes_.next());
    return channel ? channel->event_table() : 0;
}

// 0x00419AF0
float AnimBlenderComponent::fetch_value(uint32_t offset)
{
    Channel* channel = masked(lanes_.next());
    return channel ? channel->fetch_value(offset) : 0.0f;
}

// 0x00419B10
bool AnimBlenderComponent::has_value(uint32_t offset)
{
    Channel* channel = masked(lanes_.next());
    return channel ? channel->has_value(offset) : false;
}

// 0x00419CD0
bool AnimBlenderComponent::is_clip_done()
{
    Channel* channel = masked(lanes_.next());
    return channel ? channel->is_clip_done() : false;
}

// 0x00419F80
float AnimBlenderComponent::get_timeline18()
{
    const Channel* channel = masked(lanes_.next());
    if (!channel)
        return 0.0f;
    memory::ListNode* timeline = channel->timelines().next();
    if (is_over(timeline))
        return 0.0f;
    return static_cast<gfx::AnimTimeline*>(timeline->data())->ongoing();
}

// 0x0041A0A0
uint32_t AnimBlenderComponent::get_timeline30()
{
    const Channel* channel = masked(lanes_.next());
    if (!channel)
        return 0;
    memory::ListNode* timeline = channel->timelines().next();
    if (is_over(timeline))
        return 0;
    return static_cast<gfx::AnimTimeline*>(timeline->data())->anim();
}

// 0x0041A0E0
void AnimBlenderComponent::get_times(float* start, float* current, float* end)
{
    const Channel* channel = masked(lanes_.next());
    if (!channel)
        return;
    float s = 0.0f, c = 0.0f, e = 0.0f;
    memory::ListNode* timeline = channel->timelines().next();
    if (!is_over(timeline))
        static_cast<gfx::AnimTimeline*>(timeline->data())->get_times(&s, &c, &e);
    *start = s;
    *current = c;
    *end = e;
}

// 0x00419CF0
void get_channel_times(gfx::BlendChannel* channel, float* start, float* current, float* end)
{
    *start = 0.0f;
    *current = 0.0f;
    *end = 0.0f;
    memory::ListNode* timeline = channel->timelines().next();
    if (!is_over(timeline))
        static_cast<gfx::AnimTimeline*>(timeline->data())->get_times(start, current, end);
}

// 0x004196B0
float larger_of(float a, float b) { return a > b ? a : b; }

} // namespace objects
