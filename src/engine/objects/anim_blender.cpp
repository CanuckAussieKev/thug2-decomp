// The animation blender component and the pieces of its channels' links kept with it.

#include "engine/objects/anim_blender.h"

#include <bit>
#include <cstdint>
#include <cstring>

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
#include "engine/core/code_address.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/flexible_particle.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/skeleton_component.h"
#include "engine/objects/model_component.h"
#include "engine/gfx/model.h"

namespace objects {

uint32_t g_anim_blender_member_functions[180]{
    0xCABB44AE, 0x7149B753, 0, 0, 0, 0, 0xB628A959, 0x7149B753, 0, 0, 0, 0, 0xAFC2493E, 0x7149B753, 0, 0, 0, 0,
    0xF1D4B29F, 0x7149B753, 0, 0, 0, 0, 0xD58E2AD5, 0x7149B753, 0, 0, 0, 0, 0x98A32669, 0x7149B753, 0, 0, 0, 0,
    0x58C52F64, 0x7149B753, 0, 0, 0, 0, 0x2CB9AD09, 0x7149B753, 0, 0, 0, 0, 0x2889C6C9, 0x7149B753, 0, 0, 0, 0,
    0x76CC99D5, 0x7149B753, 0, 0, 0, 0, 0xB5FB7EB6, 0x7149B753, 0, 0, 0, 0, 0x1A0F9646, 0x7149B753, 0, 0, 0, 0,
    0x5195BCBB, 0x7149B753, 0, 0, 0, 0, 0x68C86AEC, 0x7149B753, 0, 0, 0, 0, 0x80FDBDF2, 0x7149B753, 0, 0, 0, 0,
    0x42FFC3CE, 0x7149B753, 0, 0, 0, 0, 0x922E7D14, 0x7149B753, 0, 0, 0, 0, 0x74CF5C94, 0x7149B753, 0, 0, 0, 0,
    0x9069F357, 0x7149B753, 0, 0, 0, 0, 0x1DF59A54, 0x7149B753, 0, 0, 0, 0, 0xC68365EB, 0x7149B753, 0, 0, 0, 0,
    0x83654874, 0x7149B753, 0, 0, 0, 0, 0xD63A1B81, 0x7149B753, 0, 0, 0, 0, 0xBD4EDD44, 0x7149B753, 0, 0, 0, 0,
    0x6AAEB76F, 0x7149B753, 0, 0, 0, 0, 0xF5E2B871, 0x7149B753, 0, 0, 0, 0, 0x986D274E, 0x7149B753, 0, 0, 0, 0,
    0x27A98022, 0x7149B753, 0, 0, 0, 0, 0x6ECEB234, 0x7149B753, 0, 0, 0, 0, 0,          0x7149B753, 0, 0, 0, 0};

namespace {

constexpr uint32_t kId = 0x5CFCCF35; // SubsetId
constexpr uint32_t kPlayAnim = 0x0B1E7291, kObjPlayAnim = 0x2CB9AD09;
constexpr float kMinWeight = 0.00001f;

bool is_head(const memory::ListNode* node) { return node->data == reinterpret_cast<void*>(-1); }

using Channel = gfx::BlendChannel;
memory::ListNode* get_link(Channel* channel) { return reinterpret_cast<memory::ListNode*>(channel); }
Channel* as_channel(memory::ListNode* node) { return reinterpret_cast<Channel*>(node); }
// A node as the original masks it: null when it's the head.
Channel* masked(memory::ListNode* node) { return is_head(node) ? nullptr : as_channel(node); }
// The next channel after `channel` (or the head).
memory::ListNode* get_next(Channel* channel) { return get_link(channel)->next; }
// The walks' end test: the head or null.
bool is_over(memory::ListNode* node) { return is_head(node) || !node; }

template <typename T>
T& prop(void* object, int offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}
uint32_t& ident_no(Channel* channel) { return channel->ident_; }

// A bool read from a 32-bit argument slot as the callee does: its low byte (the callers pass raw words).
bool bool_arg(uint32_t word) { return (word & 0xFF) != 0; }
bool bool_arg(float word) { return bool_arg(std::bit_cast<uint32_t>(word)); }

} // namespace

// 0x0041A5B0
memory::owner<ObjectModule*> AnimBlenderComponent::fabricate() { return new AnimBlenderComponent; }

// 0x00419690
void AnimBlenderComponent::publish_method_table()
{
    script::register_member_function_table(reinterpret_cast<void*>(kType), g_anim_blender_member_functions, 0x18);
}

// 0x0041A410
AnimBlenderComponent::AnimBlenderComponent()
{
    lanes_.make_head();
    auto* sentinel = static_cast<void**>(memory::alloc(0xC));
    if (sentinel) {
        sentinel[1] = static_cast<void*>(sentinel);
        sentinel[2] = static_cast<void*>(sentinel);
    }
    observers_ = static_cast<void*>(sentinel);
    kind_tag_ = kType;
}

// 0x0041A4C0 (0x0041A610 the deleting one)
AnimBlenderComponent::~AnimBlenderComponent()
{
    lists::delete_each<Channel>(&lanes_, [](Channel* channel) { channel->teardown(1); }); // (0x0057A2F0)
    auto* const* sentinel = static_cast<void**>(observers_);
    for (auto* const* node = sentinel ? static_cast<void**>(sentinel[1]) : nullptr;
         static_cast<const void*>(node) != observers_; node = static_cast<void**>(node[1])) {
        delete static_cast<BlendObserver*>(node[0]);
    }
    // the list emptied (std::list::erase(begin, end), 0x004ECC30) - twice, as the original
    for (int pass = 0; pass < 2; ++pass) {
        auto** head = static_cast<void**>(observers_);
        void* result = nullptr;
        reinterpret_cast<objects::PointerList*>(&observers_)
            ->get_erase(reinterpret_cast<objects::PointerListNode**>(&result),
                        reinterpret_cast<objects::PointerListNode*>(head ? head[1] : nullptr),
                        reinterpret_cast<objects::PointerListNode*>(head));
    }
    memory::Hub::get_pull()->dealloc(observers_);
    lanes_.uncouple(); // (0x00419D70)
}

// 0x0041A260 (the 0xCE0CA665 component's answer goes to each channel as a dword whose high byte, as in the
// original, may not be clear - only its low byte is meant)
void AnimBlenderComponent::tick_frame()
{
    uint32_t highest_set = 0;
    int count = 0;
    bool changed = false;
    Channel* channel = masked(lanes_.next);
    const bool ready = static_cast<DormancyModule*>(component18_)->wants_animating(&distance_squared_);
    if (!channel)
        return;
    do {
        if (!(std::bit_cast<float>(channel->ongoing_) >= kMinWeight) && count != 0) {
            // a channel after the first with (next to) no weight: only reset
            channel->update_blend();
            channel->update_timelines();
            gfx::g_blend_sets[count] = static_cast<uint32_t>(channel->rank_);
            if (!(highest_set > gfx::g_blend_sets[count]))
                highest_set = gfx::g_blend_sets[count];
            gfx::g_blend_weights[count] = 0.0f;
        } else {
            if (channel->tick_frame(count, ready))
                changed = true;
            gfx::g_blend_weights[count] = std::bit_cast<float>(channel->ongoing_);
            gfx::g_blend_sets[count] = static_cast<uint32_t>(channel->rank_);
            if (!(highest_set > gfx::g_blend_sets[count]))
                highest_set = gfx::g_blend_sets[count];
        }
        memory::ListNode* next = get_next(channel);
        ++count;
        if (is_over(next))
            break;
        channel = as_channel(next);
    } while (true);
    if (!changed)
        return;
    // (the skeleton component's +0x18, through the getter folded at 0x005480F0)
    auto skeleton = [this]() {
        return reinterpret_cast<gfx::Rig*>(static_cast<objects::BoneRigModule*>(bone_rig_)->bone_rig_);
    };
    if (count == 1) {
        notify_observers(gfx::g_blend_buffer);
    } else {
        const uint32_t sets = highest_set + 1;
        for (uint32_t set = 0; set != sets;) {
            gfx::Stance* pose = gfx::g_blend_buffer;
            auto bones = static_cast<uint32_t>(skeleton()->get_num_bones());
            gfx::blend_poses_by_id(gfx::g_blend_buffer, gfx::g_blend_weights, gfx::g_blend_sets,
                                   static_cast<int>(bones), pose, count, set);
            ++set;
            gfx::g_blend_sets[0] = set;
            gfx::g_blend_weights[0] = 0.0f;
            if (!(set < sets))
                break;
        }
        notify_observers(gfx::g_blend_buffer);
    }
    skeleton()->tick_frame(gfx::g_blend_buffer);
}

// 0x00419DD0
void AnimBlenderComponent::configure_from_data(script::Struct* params)
{
    if (lanes_.prev != &lanes_)
        return;
    void* memory = memory::alloc_zeroed(sizeof(Channel));
    Channel* channel = memory ? memory::construct_in<gfx::BlendChannel>(memory, entity_, this, true) : nullptr;
    get_link(channel)->prev = &lanes_;
    get_link(channel)->next = lanes_.next;
    lanes_.next->prev = get_link(channel);
    lanes_.next = get_link(channel);
    memory::enter_heap_scope(memory::bottom_up_heap());
    memory::ListNode* first = lanes_.next;
    if (!is_over(first)) {
        as_channel(first)->configure_from_data(params);
        as_channel(first)->set_events_on(true);
    }
    memory::leave_heap_scope();
}

// 0x004196D0
void AnimBlenderComponent::seal()
{
    for (memory::ListNode* node = lanes_.next; !is_over(node); node = node->next)
        as_channel(node)->seal();
    bone_rig_ = entity_->fetch_module(0x222756D5);
    mesh_instance_ = entity_->fetch_module(0x286A8D26);
    component18_ = entity_->fetch_module(0xCE0CA665);
}

// 0x00419980
void AnimBlenderComponent::step_wait_state(script::Script* script, uint32_t id)
{
    for (memory::ListNode* node = lanes_.next; !is_over(node); node = node->next) {
        if (ident_no(as_channel(node)) == id)
            as_channel(node)->release_waiting_script(script);
    }
}

// 0x00419950
int AnimBlenderComponent::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    if (name != 0x0B1E7291 && name != 0xEA6D0EFD)
        return 2;
    return pass_to_channel(name, params, script);
}

// 0x0041A1A0 (no channel to take the anim set from: a crash, as in the original)
int AnimBlenderComponent::add_channel_from_script(uint32_t, script::Struct* params, script::Script*)
{
    uint32_t id = 0;
    const Channel* first = masked(lanes_.next);
    params->read_hash_value(kId, &id, 0);
    params->put_name_key(0x52A864B0, first->lookup_key_);
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
    Channel* first = masked(lanes_.next);
    uint32_t wait_for = 0;
    if (params && (params->read_hash_value(kId, &id, 0), id != 0))
        wait_for = id;
    else
        wait_for = ident_no(first);
    script->set_wait_on_component(static_cast<script::BlockReason>(6), this, wait_for);
    return 1;
}

// 0x00419750
int AnimBlenderComponent::remove_channel(uint32_t, script::Struct* params, script::Script*)
{
    uint32_t id = 0;
    Channel* channel = masked(lanes_.next);
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
    Channel* channel = masked(lanes_.next);
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
    params->fetch_float(0x4D786F62, &a, 1);
    params->fetch_float(0xBF237659, &b, 0);
    uint32_t target = 0; // (blend_to's target is a float here: its bits passed on)
    std::memcpy(&target, &a, sizeof(target));
    channel->blend_to(target, b);
    return 1;
}

// 0x00419F10 (not a Ghidra function)
int AnimBlenderComponent::channel_exists(uint32_t, script::Struct* params, script::Script* script)
{
    uint32_t id = 0;
    params->read_hash_value(kId, &id, 0);
    for (memory::ListNode* node = lanes_.next; !is_over(node); node = node->next) {
        if (ident_no(as_channel(node)) == id) {
            script->args_->put_name_key(0u, 0xC7C6228C); // (an unnamed flag)
            return 1;
        }
    }
    return 0;
}

// 0x004198D0 (no id given and no channel: a call on null, a crash, as in the original)
int AnimBlenderComponent::pass_to_channel(uint32_t name, script::Struct* params, script::Script* script)
{
    uint32_t id = 0;
    Channel* channel = masked(lanes_.next);
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
        const int flip = skater::thaw_bmx::play_flip(entity_);
        if (flip >= 0)
            channel->set_flipped(entity_->ident_, bool_arg(static_cast<uint32_t>(flip)), 0u, bool_arg(0u));
    }
    return channel->invoke_method_by_name(name, params, script);
}

// 0x00419D40
gfx::BlendChannel* AnimBlenderComponent::find_channel(uint32_t id)
{
    for (memory::ListNode* node = lanes_.next; !is_over(node); node = node->next) {
        if (ident_no(as_channel(node)) == id)
            return as_channel(node);
    }
    return nullptr;
}

// 0x00419FD0 (the original first looks for a channel with the id and drops the result)
gfx::BlendChannel* AnimBlenderComponent::add_channel(uint32_t id, bool at_front)
{
    void* memory = memory::alloc_zeroed(sizeof(Channel));
    Channel* channel = memory ? memory::construct_in<gfx::BlendChannel>(memory, entity_, this, false) : nullptr;
    ident_no(channel) = id;
    channel->seal();
    memory::ListNode* link = get_link(channel);
    if (at_front) {
        link->prev = &lanes_;
        link->next = lanes_.next;
        lanes_.next->prev = link;
        lanes_.next = link;
    } else {
        link->prev = lanes_.prev;
        link->next = &lanes_;
        lanes_.prev->next = link;
        lanes_.prev = link;
    }
    return channel;
}

// 0x0041A220
void AnimBlenderComponent::notify_observers(gfx::Stance* pose)
{
    auto* const* sentinel = static_cast<void**>(observers_);
    for (auto* const* node = sentinel ? static_cast<void**>(sentinel[1]) : nullptr;
         static_cast<const void*>(node) != observers_; node = static_cast<void**>(node[1]))
        static_cast<BlendObserver*>(node[0])->on_pose(pose);
}

// 0x004199C0
void AnimBlenderComponent::visit_channels()
{
    for (memory::ListNode* node = lanes_.next; !is_over(node); node = node->next) {
        // (0x005480A0: an empty function)
    }
}

// 0x004199F0
void AnimBlenderComponent::pass_to_each_channel(uint32_t value)
{
    for (memory::ListNode* node = lanes_.next; !is_over(node); node = node->next)
        as_channel(node)->advance_clock(std::bit_cast<float>(value));
}

// 0x00419A20 (with no channel, the original reads the head's next from null: a crash)
void AnimBlenderComponent::reset_channels()
{
    for (memory::ListNode* node = lanes_.next; !is_over(node); node = node->next)
        as_channel(node)->get_reset();
    memory::ListNode* node = get_link(masked(lanes_.next))->next;
    if (is_over(node))
        return;
    while (node) {
        memory::ListNode* next = node->next;
        memory::ListNode* following = is_head(next) ? nullptr : next;
        as_channel(node)->teardown(1);
        node = following;
    }
}

// 0x00419C10
bool AnimBlenderComponent::any_channel(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    bool any = false;
    for (memory::ListNode* node = lanes_.next; !is_over(node); node = node->next) {
        if (as_channel(node)->set_flipped(a, bool_arg(b), c, bool_arg(d)))
            any = true;
    }
    return any;
}

// 0x00419C70
bool AnimBlenderComponent::all_channels_flag29()
{
    bool all = true;
    for (memory::ListNode* node = lanes_.next; !is_over(node); node = node->next)
        all = all && ((as_channel(node)->flag_bits_ >> 29) & 1);
    return all;
}

// 0x00419CA0
bool AnimBlenderComponent::model_ready()
{
    const render::RenderModel* thing = static_cast<MeshInstanceModule*>(mesh_instance_)->mesh_instance_;
    if (!thing)
        return false;
    const uint8_t flags = thing->flag_bits_;
    return (flags & 2) && !(flags & 4);
}

// 0x00419730
void AnimBlenderComponent::first_channel_47e_ff0()
{
    if (Channel* channel = masked(lanes_.next))
        channel->blend_first_pose();
}

// 0x00419A90
void AnimBlenderComponent::play_animation(uint32_t anim, uint32_t b)
{
    if (Channel* channel = masked(lanes_.next))
        channel->play_animation(anim, std::bit_cast<float>(b));
}

// 0x00419B50
void AnimBlenderComponent::play_animation_ex(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f,
                                             uint32_t g)
{
    if (Channel* channel = masked(lanes_.next))
        channel->play_anim(a, bool_arg(b), std::bit_cast<float>(c), std::bit_cast<float>(d), static_cast<int>(e),
                           std::bit_cast<float>(f), std::bit_cast<float>(g), nullptr);
}

// 0x00419B90
void AnimBlenderComponent::first_channel_47e_850(float a, float b, float c, float d)
{
    if (Channel* channel = masked(lanes_.next))
        channel->set_speed(a, bool_arg(b), bool_arg(c), bool_arg(d));
}

// 0x00419BB0
void AnimBlenderComponent::first_channel_47e_810(uint32_t a)
{
    if (Channel* channel = masked(lanes_.next))
        channel->get_reverse(a);
}

// 0x00419BD0
void AnimBlenderComponent::first_channel_47e_280(float a, uint32_t b)
{
    if (Channel* channel = masked(lanes_.next))
        channel->choose_wobble_goal(a, bool_arg(b));
}

// 0x00419BF0
void AnimBlenderComponent::first_channel_47e_450(float* a, uint32_t b)
{
    if (Channel* channel = masked(lanes_.next))
        channel->set_wobble_details(a, bool_arg(b));
}

// 0x00419B30
void AnimBlenderComponent::set_flag_4c(bool value)
{
    if (Channel* channel = masked(lanes_.next))
        channel->set_flag_4c(value);
}

// 0x00419AB0
uint32_t AnimBlenderComponent::get_anim_set()
{
    const Channel* channel = masked(lanes_.next);
    return channel ? channel->lookup_key_ : 0;
}

// 0x00419AD0
uint32_t AnimBlenderComponent::get_first_channel58()
{
    const Channel* channel = masked(lanes_.next);
    return channel ? channel->event_table_ : 0;
}

// 0x00419AF0
float AnimBlenderComponent::fetch_value(uint32_t offset)
{
    Channel* channel = masked(lanes_.next);
    return channel ? channel->fetch_value(offset) : 0.0f;
}

// 0x00419B10
bool AnimBlenderComponent::has_value(uint32_t offset)
{
    Channel* channel = masked(lanes_.next);
    return channel ? channel->has_value(offset) : false;
}

// 0x00419CD0
bool AnimBlenderComponent::is_clip_done()
{
    Channel* channel = masked(lanes_.next);
    return channel ? channel->is_clip_done() : false;
}

// 0x00419F80
float AnimBlenderComponent::get_timeline18()
{
    const Channel* channel = masked(lanes_.next);
    if (!channel)
        return 0.0f;
    memory::ListNode* timeline = channel->timelines_.next;
    if (is_over(timeline))
        return 0.0f;
    return static_cast<gfx::AnimTimeline*>(timeline->data)->ongoing_;
}

// 0x0041A0A0
uint32_t AnimBlenderComponent::get_timeline30()
{
    const Channel* channel = masked(lanes_.next);
    if (!channel)
        return 0;
    memory::ListNode* timeline = channel->timelines_.next;
    if (is_over(timeline))
        return 0;
    return static_cast<gfx::AnimTimeline*>(timeline->data)->anim_;
}

// 0x0041A0E0
void AnimBlenderComponent::get_times(float* start, float* current, float* end)
{
    const Channel* channel = masked(lanes_.next);
    if (!channel)
        return;
    float s = 0.0f, c = 0.0f, e = 0.0f;
    memory::ListNode* timeline = channel->timelines_.next;
    if (!is_over(timeline))
        static_cast<gfx::AnimTimeline*>(timeline->data)->get_times(&s, &c, &e);
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
    memory::ListNode* timeline = channel->timelines_.next;
    if (!is_over(timeline))
        static_cast<gfx::AnimTimeline*>(timeline->data)->get_times(start, current, end);
}

// 0x004196B0
float larger_of(float a, float b) { return a > b ? a : b; }

// 0x0041A680
maths::Vec4 VectorAt1c::get_pull() const { return vector_; }

} // namespace objects
