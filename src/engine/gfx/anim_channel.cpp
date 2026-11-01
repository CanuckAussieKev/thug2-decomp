// The animation blender's channel (gfx::CBlendChannel, anim_blend.h): its timelines, blend value, wobble, speed, flip
// and loop messages, poses (0x0047DC40-0x0047F24E). From the disassembly.

#include "engine/gfx/anim_blend.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "engine/lists/chain_start.h"
#include "engine/gfx/anim_track.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/frame_source.h"
#include "engine/gfx/model_utils.h"
#include "engine/gfx/rig.h"
#include "engine/maths/maths.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/net/player_info.h"
#include "engine/net/skater_messages.h"
#include "engine/util/byte_stream.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/cutscene.h"
#include "engine/objects/movie_manager.h"
#include "engine/objects/entity.h"
#include "engine/objects/skeleton_component.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/sys/startup_init.h"

namespace gfx {

Stance* g_blend_buffer;
float* g_blend_weights;
uint32_t* g_blend_sets;
namespace {

using script::Struct;

constexpr uint32_t kSkeleton = 0x222756D5;    // skeleton
constexpr uint32_t kAnimScratch = 0x7C0B34B4; // AnimScratch
constexpr uint32_t kFlipped = 0x20000000;
constexpr float kFramesPerSecond = 60.0f;        // 0x00645EA0
constexpr float kSecondsPerFrame = 1.0f / 60.0f; // 0x006463B8

constexpr uint32_t kDefault = 0x1CA1FF20;   // Default
constexpr uint32_t kAnim = 0x98549BA4;      // anim
constexpr uint32_t kSpeed = 0xF0D90109;     // Speed
constexpr uint32_t kCurrent = 0x230CCBF4;   // Current
constexpr uint32_t kBackwards = 0xF8CFD515; // Backwards
constexpr uint32_t kId = 0x40C698AF;        // id
constexpr uint32_t kSubsetId = 0x5CFCCF35;  // SubsetId
constexpr uint32_t kAnimTags = 0x5DB4115F;  // AnimTags
constexpr uint32_t kIndex = 0xD2D6A582;     // (a timeline's index)
constexpr uint32_t kPlayAnim = 0x0B1E7291, kObjPlayAnim = 0x2CB9AD09;
constexpr uint32_t kCycle = 0x4F792E6C, kPingPong = 0x3153E314, kHoldLoop = 0x6D941203; // the loop types' names
constexpr float kStockCrossfade = 0.3f;                                                 // 0x3E99999A
constexpr float kFixedPoint = 4096.0f;                                                  // 0x00648730

// The network messages a channel sends (engine/net/skater_messages.h).
using online::kMsgFlip;
using online::kMsgLoopType;
using online::kMsgPlayAnim;
using online::kMsgWobbleDetails;
using online::kMsgWobbleTarget;

uint8_t g_last_wobble_target{0xFF};
uint8_t g_last_wobble_details{0xFF};
int8_t g_last_loop_type{-1};
Stance* g_poses;
int g_max_blends;
int g_subsets;

// What was last sent (all start 0xFF).
uint8_t& last_wobble_target() { return g_last_wobble_target; }
uint8_t& last_wobble_details() { return g_last_wobble_details; }
int8_t& last_loop_type() { return g_last_loop_type; }

// The blend buffers (init_buffers).
uint32_t*& blend_sets() { return g_blend_sets; }
float*& blend_weights() { return g_blend_weights; }
Stance*& blend_buffer() { return g_blend_buffer; }
Stance*& current_poses() { return g_poses; }
int& max_blends() { return g_max_blends; }
int& get_subsets() { return g_subsets; }
bool omit_from_draw() { return render::g_suppress_draw; }

memory::PoolPair& struct_pools() { return script::g_struct_pools; }

float& get_as_float(uint32_t& bits) { return reinterpret_cast<float&>(bits); }

bool is_head(const memory::ListNode* node) { return node->data == reinterpret_cast<void*>(-1); }
// A list node as the original masks it: null when it's the head.
AnimTrack* masked(memory::ListNode* node) { return is_head(node) ? nullptr : reinterpret_cast<AnimTrack*>(node); }
memory::ListNode* elem(AnimTrack* timeline) { return reinterpret_cast<memory::ListNode*>(timeline); }

// The playing timeline (the list's first), or null.
AnimTrack* get_first(BlendChannel* channel)
{
    memory::ListNode* node = channel->timelines_.next;
    return is_head(node) || !node ? nullptr : reinterpret_cast<AnimTrack*>(node);
}

void uncouple(memory::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

// A timeline (an AnimTrack, its own node) deleted: its scalar deleting destructor (the original's slot 0).
void delete_timeline(memory::ListNode* node) { static_cast<AnimTrack*>(node->data)->teardown(1); }
// Every timeline of the list deleted (0x0057A2F0 over the list).
void delete_all_timelines(memory::ListNode* head)
{
    lists::delete_each<AnimTrack>(head, [](AnimTrack* timeline) { timeline->teardown(1); });
}

objects::GameEntity* get_owner(BlendChannel* channel)
{
    return reinterpret_cast<objects::GameEntity*>(channel->owner_ptr_);
}

// The network manager (0x007CCE14) and this machine's client app for the channel's object - null when the object
// isn't this machine's player's skater.
online::Hub* net_session_slot() { return online::g_online_hub; }
online::PlayerRecord* find_own_player_record(uint32_t object_id)
{
    auto* player = reinterpret_cast<online::PlayerRecord*>(
        net_session_slot()->find_player_slot_by_obj(static_cast<uint16_t>(object_id)));
    if (!player || !player->is_own_player())
        return nullptr;
    return player;
}
net::Guest* client_of(online::PlayerRecord* player)
{
    return static_cast<net::Guest*>(net_session_slot()->fetch_patron(static_cast<int>(player->get_skater_view_slot())));
}

void post_out(net::Guest* app, net::NetMessageInfo& message, uint8_t id, void* data, uint16_t length, bool singular)
{
    message.content_ = data;
    message.byte_count_ = length;
    message.ident_ = id;
    if (singular)
        message.singular_ = true;
    app->send_to_host(&message);
}

// A structure made and freed as the original's inlined new / delete do.
Struct* spawn_param_bag()
{
    void* memory = struct_pools().carve(8);
    return memory ? new (memory) script::Struct : nullptr;
}
void free_struct(Struct* params)
{
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

// The playing timeline's controllers given a member function with `params` (slot 5, no script).
void pass_to_first(BlendChannel* channel, uint32_t name, Struct* params)
{
    masked(channel->timelines_.next)->invoke_method_by_name(name, params, nullptr);
}

// The original's _ftol: truncated towards zero.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The track's fade (start_fade 0x00481B40), weight (0x00481B80, ST0) and operator new (0x00481EE0; the original
// passes it 0 too).
bool start_blend_out(AnimTrack* timeline, float period) { return timeline->start_fade(period); }
double current_weight(AnimTrack* timeline) { return timeline->get_weight(); }
void* new_timeline(uint32_t size) { return AnimTrack::operator new(size); }

int count_list_items(memory::ListNode* head) { return reinterpret_cast<AnimListHead*>(head)->tally_entries(); }

// An animation's frame data in the anim set (render::get_anim_data 0x0048CF50: its first value the length), the range
// (fetch_anim_durations 0x004877E0 - the original passes the script too, unused) and loop type (get_anim_loop_type 0x00487770)
// a play's parameters ask for, and a controller added to a track (0x00481FB0).
float* anim_length(uint32_t key) { return static_cast<float*>(render::get_anim_data(key, 0)); }
void range_from_params(float* from, float* to, float current, float length, Struct* params, script::Script* /*script*/)
{
    fetch_anim_durations(from, to, current, length, params);
}
void loop_type_from_params(int* loop_type, Struct* params) { get_anim_loop_type(loop_type, params); }
void attach_controller(AnimTrack* timeline, Struct* params) { timeline->attach_controller(params); }

// The skeleton component's skeleton (the getter folded at 0x005480F0).
Rig* skeleton_of(objects::ObjectModule* component)
{
    return reinterpret_cast<Rig*>(static_cast<objects::BoneRigModule*>(component)->bone_rig_);
}

} // namespace

// 0x0047DD10
int AnimListHead::tally_entries()
{
    const memory::ListNode* node = head_.next;
    int count = 0;
    while (!is_head(node) && node) {
        node = node->next;
        ++count;
    }
    return count;
}

// --- making and destroying ---

// 0x0047F150
BlendChannel::BlendChannel(objects::GameEntity* object, objects::AnimBlenderComponent* blender, bool full)
    : owner_ptr_(reinterpret_cast<uint8_t*>(object)), blender_(blender)
{
    list_link_.make_node(this);
    timelines_.make_head();
    const uint32_t value = full ? 0x3F800000 : 0;
    ongoing_ = value;
    aim_point_ = value;
    outset_ = value;
    get_reset();
}

// 0x0047EE90
void BlendChannel::destroy_body()
{
    delete_all_timelines(&timelines_);
    timelines_.uncouple();
    list_link_.uncouple();
}

// 0x0047F230
BlendChannel* BlendChannel::teardown(uint8_t flags)
{
    destroy_body();
    if (flags & 1)
        memory::dealloc(this);
    return this;
}

// 0x0047EF20
void BlendChannel::get_reset()
{
    delete_all_timelines(&timelines_);
    const bool waiting = waiting_;
    has_next_blend_ = false;
    next_blend_ = 0.0f;
    if (waiting) {
        waiting_ = false;
        if (script::Script* script = get_owner(this)->own_script_)
            script->stop_waiting();
    }
    wait_time_ = 0.0f;
    prev_timeline_pos_ = 0.0f;
    flag_4e_ = false;
}

// 0x0047DCF0
void BlendChannel::seal() { bone_rig_ = get_owner(this)->fetch_module(kSkeleton); }

// --- each frame ---

// 0x0047EFB0
bool BlendChannel::tick_frame(int index, bool blend)
{
    update_blend();
    update_timelines();
    remove_done_timelines();
    if (blend && blender_->model_ready())
        return blend_pose(index);
    return false;
}

// 0x0047EF60
void BlendChannel::release_waiting_script(script::Script* script)
{
    if (script->suspend_reason_ != 6)
        return;
    if (AnimTrack* first = get_first(this)) {
        if (first->is_accomplished())
            script->stop_waiting();
    }
}

// 0x0047E030 (x87: the blend's time left unrounded through the division)
void BlendChannel::update_blend()
{
    if (const AnimTrack* first = get_first(this))
        prev_timeline_pos_ = first->ongoing_;
    if (period_left_ > 0.0f) {
        double left = period_left_ - frame_clock::get_slomo_frame_seconds();
        period_left_ = static_cast<float>(left);
        if (!(left > 0.0))
            left = 0.0;
        period_left_ = static_cast<float>(left);
        const double done = left / period_;
        get_as_float(ongoing_) = static_cast<float>(
            (static_cast<double>(get_as_float(outset_)) - get_as_float(aim_point_)) * done + get_as_float(aim_point_));
    } else {
        ongoing_ = aim_point_;
    }
    if (!waiting_)
        return;
    objects::GameEntity* owner = get_owner(this);
    if (!owner->own_script_) {
        owner->own_script_ = script::Script::create();
    }
    bool stop = false;
    if (!owner->own_script_->is_blocked(false)) {
        stop = true;
    } else {
        float start = 0.0f, current = 0.0f, end = 0.0f;
        objects::get_channel_times(this, &start, &current, &end);
        // (forwards: the time reached; backwards - or a range of none - reached the other way; NaN: never)
        if (start <= end && current >= wait_time_)
            stop = true;
        else
            stop = start >= end && current <= wait_time_;
    }
    if (stop) {
        owner->own_script_->stop_waiting();
        waiting_ = false;
    }
}

// 0x0047E1B0
void BlendChannel::update_timelines()
{
    int count = count_list_items(&timelines_);
    AnimTrack* timeline = masked(timelines_.next);
    for (; count > 0; --count) {
        timeline->tick_frame();
        timeline = masked(elem(timeline)->next);
    }
}

// 0x0047DDC0
void BlendChannel::remove_done_timelines()
{
    memory::ListNode* node = timelines_.next;
    if (is_head(node) || !node)
        return;
    while (node) {
        AnimTrack* next = masked(node->next);
        if (!reinterpret_cast<AnimTrack*>(node)->is_operative()) {
            uncouple(node);
            delete_timeline(node);
        }
        node = elem(next);
    }
}

// 0x0047DE20
void BlendChannel::push_timeline(float period)
{
    int count = 0;
    for (AnimTrack* timeline = masked(timelines_.next); timeline;) {
        AnimTrack* next = masked(elem(timeline)->next);
        if (++count >= max_blends()) {
            uncouple(elem(timeline));
            delete_timeline(elem(timeline));
        }
        timeline = next;
    }
    if (count_list_items(&timelines_) != 0) {
        AnimTrack* first = get_first(this);
        if (first && start_blend_out(first, period)) {
            float others = 0.0f;
            for (memory::ListNode* node = elem(first)->next; !is_head(node) && node; node = node->next) {
                auto* timeline = reinterpret_cast<AnimTrack*>(node);
                if (timeline->is_fading())
                    others =
                        static_cast<float>(static_cast<double>(timeline->fade_scale_) * timeline->fade_left_ + others);
            }
            first->fade_scale_ = static_cast<float>((1.0 - others) / first->fade_left_);
        }
    }
    void* memory = new_timeline(sizeof(AnimTrack));
    AnimTrack* timeline = memory ? ::new (memory) AnimTrack(get_owner(this)) : nullptr;
    memory::ListNode* node = elem(timeline); // (null: a crash, as the original)
    node->prev = &timelines_;
    node->next = timelines_.next;
    timelines_.next->prev = node;
    timelines_.next = node;
}

// 0x0047ED60
bool BlendChannel::blend_pose(int index)
{
    if (omit_from_draw() || !lookup_key_)
        return false;
    Rig* skeleton = skeleton_of(bone_rig_);
    if (!skeleton)
        return false;
    skeleton->set_lod(blender_->distance_squared_);
    const int count = count_list_items(&timelines_);
    const RigData* data = skeleton->payload_;
    for (int i = 0; i < count; ++i) {
        // (each pose's translations start as the skeleton's bind translations)
        std::memcpy(current_poses()[i].translations, data->bind_translations_,
                    static_cast<size_t>(skeleton->get_num_bones() & 0x0FFFFFFF) * 16);
        fetch_pose(i, &current_poses()[i], &blend_weights()[i]);
    }
    if (count > 1)
        mix_poses(current_poses(), blend_weights(), skeleton->get_num_bones(), current_poses(), count);
    std::memcpy(&blend_buffer()[index], current_poses(), sizeof(Stance));
    return true;
}

// 0x0047EFF0
bool BlendChannel::blend_first_pose() { return blend_pose(0); }

// 0x0047EAA0
void BlendChannel::fetch_pose(int index, void* pose, float* weight)
{
    AnimTrack* timeline = nullptr;
    memory::ListNode* node = timelines_.next;
    if (!is_head(node) && node) {
        for (;;) {
            if (index-- == 0) {
                timeline = reinterpret_cast<AnimTrack*>(node);
                break;
            }
            node = node->next;
            if (is_head(node) || !node)
                break;
        }
    }
    const double w = current_weight(timeline); // (null when not found: a crash, as the original)
    auto rounded = static_cast<float>(w);
    if (!(w <= 0.0)) {
        skeleton_of(bone_rig_); // (read and not used)
        timeline->get_poses(pose);
        *weight = rounded;
    } else {
        *weight = 0.0f;
    }
}

// --- what scripts and the network ask ---

// 0x0047DC40
bool BlendChannel::set_flipped(uint32_t object_id, bool flip, uint32_t /*unused*/, bool send)
{
    if (send && ident_ == 0) {
        if (online::PlayerRecord* player = find_own_player_record(object_id)) {
            net::NetMessageInfo message;
            net::Guest* app = client_of(player);
            uint8_t data[2] = {static_cast<uint8_t>(object_id), static_cast<uint8_t>(flip)};
            post_out(app, message, kMsgFlip, data, 2, false);
        }
    }
    const bool was = (flag_bits_ >> 29) & 1;
    flag_bits_ = (flag_bits_ & ~kFlipped) | (flip ? kFlipped : 0);
    return was;
}

// 0x0047DD80
void BlendChannel::toggle_flipped()
{
    net::AppHost* app = net_session_slot()->fetch_patron(0);
    const uint32_t unused = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(app) + 8);
    set_flipped(get_owner(this)->ident_, !((flag_bits_ >> 29) & 1), unused, true);
}

// 0x0047E010
void BlendChannel::advance_clock(float frames)
{
    memory::ListNode* node = timelines_.next;
    if (!is_head(node) && node)
        reinterpret_cast<AnimTrack*>(node)->advance_clock(frames);
}

// 0x0047E210
bool BlendChannel::is_clip_done()
{
    AnimTrack* first = get_first(this);
    return first ? first->is_accomplished() : false;
}

// 0x0047E240
bool BlendChannel::is_cycling()
{
    const AnimTrack* first = get_first(this);
    if (!first)
        return false;
    return first->repeat_mode_ == AnimTimeline::kCycle || first->repeat_mode_ == AnimTimeline::kPingPong;
}

// 0x0047DF50
bool BlendChannel::is_anim_one_of(Struct* params, script::Script* /*script*/)
{
    const AnimTrack* first = get_first(this);
    const uint32_t anim = first ? first->anim_ : 0;
    for (script::Component* component = params->step_to_entry(nullptr); component;
         component = params->step_to_entry(component)) {
        if (component->name)
            continue;
        script::Array* array = nullptr;
        if (component->kind_tag() == script::kQbvalName) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
            const script::SymbolTableEntry* symbol = script::resolve_symbol(component->checksum);
            if (symbol && symbol->type == script::kQbvalArray) {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
                array = reinterpret_cast<script::Array*>(symbol->raw);
            } else {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
                if (component->checksum == anim)
                    return true;
                continue;
            }
        } else if (component->kind_tag() == script::kQbvalArray) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
            array = component->array;
        } else {
            continue;
        }
        if (array) {
            for (uint32_t i = 0; i < array->extent_; ++i) {
                if (array->fetch_part(i) == anim)
                    return true;
            }
        }
    }
    return false;
}

// 0x0047E280
void BlendChannel::choose_wobble_goal(float target, bool send)
{
    if (send && ident_ == 0) {
        if (online::PlayerRecord* player = find_own_player_record(get_owner(this)->ident_)) {
            net::Guest* app = client_of(player);
            if (target < 0.0f)
                target = 0.0f;
            else if (target > 1.0f)
                target = 1.0f;
            uint8_t data[2];
            data[1] = static_cast<uint8_t>(get_truncated(target * 255.0)); // (0x00646B88)
            data[0] = static_cast<uint8_t>(get_owner(this)->ident_);
            if (last_wobble_target() != data[1]) {
                net::NetMessageInfo message;
                post_out(app, message, kMsgWobbleTarget, data, 2, true);
                last_wobble_target() = data[1];
            }
        }
    }
    if (!get_first(this))
        return;
    Struct* params = spawn_param_bag();
    params->push_float(0x4D747FA0, target);
    pass_to_first(this, 0xD0209498, params); // choose_wobble_goal
    free_struct(params);
}

// 0x0047E450
void BlendChannel::set_wobble_details(const float* details, bool send)
{
    if (send && ident_ == 0) {
        if (online::PlayerRecord* player = find_own_player_record(get_owner(this)->ident_)) {
            net::Guest* app = client_of(player);
            // Which of its known values each detail has (0x00645D6C... - the server sends those).
            uint8_t bits = 0;
            if (details[0] == 0.05f)
                bits = 0;
            else if (details[0] == 0.1f)
                bits = 1;
            else if (details[0] == 10.1f)
                bits = 2;
            const uint8_t b1 = details[1] == 0.04f ? 0 : details[1] == 10.1f ? 1 : 0;
            const uint8_t b2 = details[2] == 0.0022f ? 0 : details[2] == 20.0f ? 1 : 0;
            const uint8_t b3 = details[3] == 0.0017f ? 0 : details[3] == 10.0f ? 1 : 0;
            const uint8_t b4 = details[4] == 1.5f ? 0 : details[4] == 1.0f ? 1 : 0;
            uint8_t data[2];
            data[0] = static_cast<uint8_t>(bits | b1 << 2 | b2 << 3 | b3 << 4 | b4 << 5);
            data[1] = static_cast<uint8_t>(get_owner(this)->ident_);
            if (last_wobble_details() != data[0]) {
                net::NetMessageInfo message;
                post_out(app, message, kMsgWobbleDetails, data, 2, true);
                last_wobble_details() = data[0];
            }
        }
    }
    if (!get_first(this))
        return;
    Struct* params = spawn_param_bag();
    params->push_float(0xFD266A26, details[0]); // wobbleAmpA
    params->push_float(0x642F3B9C, details[1]); // wobbleAmpB
    params->push_float(0x0F43FD49, details[2]); // wobbleK1
    params->push_float(0x964AACF3, details[3]); // wobbleK2
    params->push_float(0xF90B0824, details[4]); // spazFactor
    pass_to_first(this, 0xEA6D0EFD, params);    // SetWobbleDetails
    free_struct(params);
}

// 0x0047E740
void BlendChannel::change_repeat_mode(int loop_type, bool send)
{
    if (send && ident_ == 0) {
        if (online::PlayerRecord* player = find_own_player_record(get_owner(this)->ident_)) {
            net::Guest* app = client_of(player);
            uint8_t data[2] = {static_cast<uint8_t>(get_owner(this)->ident_), static_cast<uint8_t>(loop_type)};
            if (last_loop_type() != loop_type) {
                net::NetMessageInfo message;
                post_out(app, message, kMsgLoopType, data, 2, false);
                last_loop_type() = static_cast<int8_t>(loop_type);
            }
        }
    }
    if (AnimTrack* first = get_first(this))
        first->repeat_mode_ = loop_type;
}

// 0x0047E810
void BlendChannel::get_reverse(uint32_t /*unused*/)
{
    if (AnimTrack* first = get_first(this))
        first->direction_ = first->direction_ != 0 ? 0 : -1;
}

// 0x0047E850 (x87: the range and the ratio's products unrounded)
void BlendChannel::set_speed(float speed, bool send, bool all, bool keep_ratio)
{
    const AnimTrack* first = get_first(this);
    if (!first)
        return;
    if (!all && std::abs(static_cast<double>(first->speed_) * kFramesPerSecond - speed) < 0.05f) // (0x00645D6C)
        return;
    if (send && ident_ == 0) {
        if (online::PlayerRecord* player = find_own_player_record(get_owner(this)->ident_)) {
            if (net::Guest* app = client_of(player)) {
                net::NetMessageInfo message;
                online::AnimSpeedMessage sent;
                sent.speed_bits = std::bit_cast<uint32_t>(speed);
                sent.object_id = static_cast<uint8_t>(get_owner(this)->ident_);
                uint8_t data[online::kAnimSpeedSize];
                util::ByteWriter writer(data, sizeof(data));
                online::write_anim_speed(writer, sent);
                post_out(app, message, online::kMsgAnimSpeed, data, sizeof(data), false);
            }
        }
    }
    if (!all) {
        get_first(this)->speed_ = speed * kSecondsPerFrame;
        return;
    }
    if (keep_ratio) {
        float start = 0.0f, current = 0.0f, end = 0.0f;
        get_first(this)->get_times(&start, &current, &end);
        auto ratio = static_cast<float>(speed / (static_cast<double>(end) - start));
        for (memory::ListNode* node = timelines_.next; !is_head(node) && node; node = node->next) {
            auto* timeline = reinterpret_cast<AnimTrack*>(node);
            timeline->get_times(&start, &current, &end);
            timeline->speed_ = static_cast<float>((static_cast<double>(end) - start) * ratio * kSecondsPerFrame);
        }
    } else {
        memory::ListNode* node = timelines_.next;
        if (is_head(node) || !node)
            return;
        const float per_frame = speed * kSecondsPerFrame;
        for (; !is_head(node) && node; node = node->next)
            reinterpret_cast<AnimTrack*>(node)->speed_ = per_frame;
    }
}

// 0x0047EA40
double BlendChannel::get_speed(Struct* /*params*/, script::Script* /*script*/)
{
    const AnimTrack* first = get_first(this);
    if (!first)
        return 0.0;
    return static_cast<double>(first->speed_) * kFramesPerSecond;
}

// 0x0047EA80
void BlendChannel::set_events_on(bool on)
{
    if (AnimTrack* first = get_first(this)) {
        events_on_ = on;
        first->events_on_ = on;
    }
}

// 0x0047F000
void BlendChannel::set_anim_tags(uint32_t anim)
{
    Struct* all = script::read_sub_struct(0x95807202);
    Struct* tags = nullptr;
    if (!all || !all->read_sub_struct(anim, &tags, 0))
        return;
    Struct* params = spawn_param_bag();
    Struct* copy = spawn_param_bag();
    copy->append_structure_if_any(tags);
    params->append_struct_ref(0x5DB4115F, copy); // AnimTags
    get_owner(this)->set_tags_from_struct(params);
    free_struct(params);
}

// 0x0047EB30
void BlendChannel::init_buffers(int blends, int subsets)
{
    max_blends() = blends;
    get_subsets() = subsets;
    debug::printf("D:%s:%i: size = %i\n", "nimblendsubset.cpp", 0x963, 0);
    memory::Hub* manager = memory::Hub::get_pull();
    manager->create_labeled_zone(kAnimScratch, get_subsets() * 0x840 + 0x90 + max_blends() * 0x848, "AnimScratch",
                                 0); // "AnimScratch"
    manager->enter_heap_scope(manager->fetch_heap_by_key(kAnimScratch, 1));
    // (each pose's rotations made the identity by the vector constructor iterator 0x00401020 with 0x00464340; its
    // translations' constructor 0x005902E0 does nothing)
    auto new_poses = [](int count) {
        auto* poses = static_cast<Stance*>(memory::alloc(static_cast<uint32_t>(count) * sizeof(Stance)));
        if (poses) {
            for (int i = 0; i < count; ++i) {
                for (auto& rotation : poses[i].rotations)
                    reinterpret_cast<objects::IdentityQuat*>(rotation)->assign_value();
            }
        }
        return poses;
    };
    blend_buffer() = new_poses(get_subsets());
    current_poses() = new_poses(max_blends());
    blend_sets() = static_cast<uint32_t*>(memory::alloc(static_cast<uint32_t>(max_blends()) * 4));
    blend_weights() = static_cast<float*>(memory::alloc(static_cast<uint32_t>(max_blends()) * 4));
    memory::Hub::get_pull()->leave_heap_scope();
}

// --- playing ---

// 0x0047F260
void BlendChannel::play_anim(uint32_t anim, bool send, float from, float to, int loop_type, float blend_period,
                             float speed, Struct* params)
{
    if (!anim)
        anim = kDefault;
    if (send && ident_ == 0) {
        if (online::PlayerRecord* player = find_own_player_record(get_owner(this)->ident_)) {
            net::NetMessageInfo message;
            net::Guest* app = client_of(player);
            // loop type, object, anim, then the times, blend period and speed x 4096 as shorts
            online::PlayAnimMessage sent;
            sent.loop_type = static_cast<uint8_t>(loop_type);
            sent.object_id = static_cast<uint8_t>(get_owner(this)->ident_);
            sent.anim = anim;
            sent.from = static_cast<uint16_t>(get_truncated(from * kFixedPoint));
            sent.to = static_cast<uint16_t>(get_truncated(to * kFixedPoint));
            sent.blend_period = static_cast<uint16_t>(get_truncated(blend_period * kFixedPoint));
            sent.speed = static_cast<uint16_t>(get_truncated(speed * kFixedPoint));
            uint8_t data[online::kPlayAnimSize];
            util::ByteWriter writer(data, sizeof(data));
            online::write_play_anim(writer, sent);
            post_out(app, message, kMsgPlayAnim, data, sizeof(data), false);
        }
    }
    if (blend_period == 0.0f || !flag_4c_)
        delete_all_timelines(&timelines_);
    push_timeline(blend_period);
    masked(timelines_.next)
        ->play_anim(anim, from, to, loop_type, blend_period, speed, ((flag_bits_ >> 29) & 1) != 0, params);
    if (Struct* tags = get_owner(this)->tag_table_)
        tags->erase_module(kAnimTags);
    set_anim_tags(anim);
}

// 0x004803D0
void BlendChannel::play_animation(uint32_t anim, float blend_period)
{
    play_anim(anim, false, 0.0f, *anim_length(lookup_key_ + anim), 0, blend_period, 1.0f, nullptr);
}

// 0x00480410
void BlendChannel::configure_from_data(Struct* params)
{
    params->read_hash_value(kSubsetId, &ident_, 0);
    params->fetch_float(0x4D786F62, &get_as_float(ongoing_), 0); // BlendWeight
    int priority = 0;
    params->fetch_int_value(0x3D836C96, &priority, 0);
    rank_ = priority;
    params->read_hash_value(0x5B8C6DC2, &event_table_, 0); // AnimEventTableName
    uint32_t anim_set = 0;                                 // (the original's is left as it was when neither is there)
    if (!params->read_hash_value(0x52A864B0, &anim_set, 0))
        params->read_hash_value(0x09794932, &anim_set, 1);
    lookup_key_ = anim_set;
    play_anim(kDefault, false, 0.0f, 1000.0f, 0, kStockCrossfade, 1.0f, nullptr);
    get_reset();
    uint32_t anim = kDefault;
    const bool loop = params->read_hash_value(0xEAE64B47, &anim, 0);
    play_anim(anim, false, 0.0f, *anim_length(lookup_key_ + anim), 0, kStockCrossfade, 1.0f, nullptr);
    if (loop)
        change_repeat_mode(AnimTimeline::kCycle, true);
    if (params->has_bare_flag(0xCF466AA2))
        flag_4c_ = true;
    set_events_on(params->has_bare_flag(0xE1B3F630));
}

// 0x0047F470 (x87: the synchronised start's fraction and product unrounded)
uint32_t BlendChannel::play_anim_from_script(Struct* params, script::Script* script, float blend_period)
{
    if (const AnimTrack* first = get_first(this))
        prev_timeline_pos_ = first->ongoing_;
    if (flag_4e_) {
        AnimTrack* first = get_first(this);
        if (!first || !first->is_accomplished())
            return 0;
    }
    if (!lookup_key_)
        return 0;
    uint32_t anim = 0;
    params->read_hash_value(kAnim, &anim, 0);
    if (!anim)
        script::dump_to_log(params, 0);
    float speed = 1.0f;
    params->fetch_float(kSpeed, &speed, 0);
    const AnimTrack* first = get_first(this);
    const uint32_t playing = first ? first->anim_ : 0;
    if (anim == kCurrent) {
        anim = playing;
    } else if (playing == anim && params->has_bare_flag(0xFE09FE09)) { // NoRestart
        return 0;
    }
    if (!anim_length(lookup_key_ + anim))
        anim = kDefault;

    float blend = blend_period;
    if (has_next_blend_) {
        blend = next_blend_;
        has_next_blend_ = false;
    } else {
        float percent = 0.0f;
        if (params->fetch_float(0x4F3ABD6C, &percent, 0)) // (of the anim's length)
            blend = static_cast<float>(percent * static_cast<double>(0.01f) * *anim_length(lookup_key_ + anim));
        else
            params->fetch_float(0x8F0D24ED, &blend, 0); // BlendPeriod
    }
    const float length = *anim_length(lookup_key_ + anim);
    float to = length;
    float from = 0.0f;
    float start = 0.0f; // the time the new one starts at
    bool sync = true;
    double fraction = 0.0;
    if (params->has_bare_flag(0x099371AE)) { // SyncToPreviousAnim: as far through as the playing one
        float current = 0.0f, ends = 0.0f;
        objects::get_channel_times(this, &ends, &current, &ends);
        float playing_length = *anim_length(lookup_key_ + playing);
        params->fetch_float(0x4B4A153A, &playing_length, 0); // (read and not used here)
        auto through = static_cast<float>(current / *anim_length(lookup_key_ + playing));
        if (through > 1.0f)
            fraction = 1.0;
        else if (through < 0.0f)
            fraction = 0.0;
        else
            fraction = through;
    } else if (params->has_bare_flag(0xFF5AC79E)) { // as far from the end as the playing one
        float current = 0.0f, ends = 0.0f;
        objects::get_channel_times(this, &ends, &current, &ends);
        float playing_length = *anim_length(lookup_key_ + playing);
        params->fetch_float(0x4B4A153A, &playing_length, 0);
        fraction = maths::confine(static_cast<float>((static_cast<double>(playing_length) - current) / playing_length),
                                  0.0f, 1.0f);
    } else {
        sync = false;
        fraction = 0.0;
    }
    if (sync) {
        start = static_cast<float>(fraction * length);
        if (params->has_bare_flag(kBackwards))
            start = static_cast<float>(-(static_cast<double>(length) - start));
        speed = static_cast<float>(get_speed(nullptr, nullptr));
    }
    float current = 0.0f, ends = 0.0f;
    if (AnimTrack* playing_now = get_first(this))
        playing_now->get_times(&ends, &current, &ends);
    range_from_params(&from, &to, current, length, params, script);
    int loop_type = 0;
    loop_type_from_params(&loop_type, params);
    flag_4e_ = params->has_bare_flag(0x84F13067); // (wait for the playing one to complete)
    float duration = 0.0f;
    if (params->fetch_float(0x23272E4D, &duration, 0))
        speed = static_cast<float>(length / duration);
    play_anim(anim, true, from, to, loop_type, blend, speed, params);
    AnimTrack* timeline = masked(timelines_.next);
    if (start != 0.0f)
        timeline->advance_clock(start);
    timeline->events_on_ = events_on_;

    uint32_t partial = 0;
    if (params->read_hash_value(0x6C565F57, &partial, 0)) { // PartialAnimOverlay: a partial animation controller too
        Struct* controller = script::spawn_param_bag();
        if (controller)
            controller = new (controller) script::Struct;
        auto id = reinterpret_cast<uint32_t>(script); // (its argument's place reused: the id when there is one)
        if (params->read_hash_value(0xF0CE9E0F, &id, 0))
            controller->put_name_key(kId, id);
        controller->put_name_key(0x7321A8D6, 0x659BF355); // Type PartialAnim
        controller->put_name_key(0x6C2BFB7F, partial);    // AnimName
        controller->push_float(0x46E55E8F, from);         // From
        controller->push_float(0x28782D3B, to);           // To
        controller->put_name_key(0u, 0xD029F619);         // Seconds
        controller->push_float(kCurrent, start);
        controller->push_float(kSpeed, speed);
        switch (loop_type) {
        case AnimTimeline::kCycle:
            controller->put_name_key(0u, kCycle);
            break;
        case AnimTimeline::kPingPong:
            controller->put_name_key(0u, kPingPong);
            break;
        case AnimTimeline::kHold:
            controller->put_name_key(0u, kHoldLoop);
            break;
        default:
            break;
        }
        attach_controller(masked(timelines_.next), controller);
        free_struct(controller);
    }
    return anim;
}

// 0x0047FA00
int BlendChannel::invoke_method_by_name(uint32_t name, Struct* params, script::Script* script)
{
    switch (name) {
    case 0x1DF59A54: { // the info of timeline Index (0xD2D6A582; the playing one without it) to the script
        int index = 0;
        if (!params->fetch_int_value(kIndex, &index, 0)) {
            memory::ListNode* node = timelines_.next;
            if (!is_head(node) && node)
                add_timeline_info(reinterpret_cast<AnimTimeline*>(node), script->args_);
            return 1;
        }
        for (AnimTrack* timeline = masked(timelines_.next); timeline; timeline = masked(elem(timeline)->next)) {
            if (index == 0) {
                add_timeline_info(timeline, script->args_);
                break;
            }
            --index;
        }
        return 1;
    }
    case 0x27A98022: { // Flip: flipped, and every timeline's (or timeline Index's) controllers told
        int index = 0;
        if (!params->fetch_int_value(kIndex, &index, 0)) {
            toggle_flipped();
            for (const memory::ListNode* node = timelines_.next; !is_head(node) && node; node = node->next)
                static_cast<AnimTrack*>(node->data)->invoke_method_by_name(name, params, script);
            return 1;
        }
        if (index == 0)
            toggle_flipped();
        for (AnimTrack* timeline = masked(timelines_.next); timeline; timeline = masked(elem(timeline)->next)) {
            if (index == 0) {
                timeline->invoke_method_by_name(name, params, script);
                break;
            }
            --index;
        }
        return 1;
    }
    case 0x2889C6C9:
    case 0x76CC99D5: // (whether the animation is complete)
        return is_clip_done() ? 1 : 0;
    case kPlayAnim:
    case kObjPlayAnim: {
        play_anim_from_script(params, script, kStockCrossfade);
        memory::ListNode* node = timelines_.next;
        if (!is_head(node) && node && reinterpret_cast<AnimTrack*>(node)->repeat_mode_ == AnimTimeline::kHold)
            reinterpret_cast<objects::Entity*>(owner_ptr_)
                ->invoke_method_by_name(0xEA6D0EFDu, params,
                                        script); // (the object's own member function: the original's slot 3)
        return 1;
    }
    case 0x42FFC3CE: { // GetAnimLength: of anim, to the script's length
        uint32_t anim = 0;
        params->read_hash_value(kAnim, &anim, 0);
        if (has_value(anim))
            script->args_->push_float(0xFE82614D, std::fabs(fetch_value(anim))); // length
        return 1;
    }
    case 0x4FAF61A9: { // every timeline's speed so its range takes 0x23272E4D seconds
        float duration = 0.0f;
        params->fetch_float(0x23272E4D, &duration, 1);
        for (memory::ListNode* node = timelines_.next; !is_head(node) && node; node = node->next) {
            auto* timeline = reinterpret_cast<AnimTrack*>(node);
            float start = 0.0f, current = 0.0f, end = 0.0f;
            timeline->get_times(&start, &current, &end);
            const float larger = start > end ? start : end;
            timeline->speed_ = static_cast<float>(larger / static_cast<double>(duration) * kSecondsPerFrame);
        }
        return 1;
    }
    case 0x58C52F64: // the loop type: once with 0xD443A2BC, else cycling
        change_repeat_mode(params->has_bare_flag(0xD443A2BC) ? AnimTimeline::kOnce : AnimTimeline::kCycle, true);
        return 1;
    case 0x5195BCBB: // the speed to the script's Speed
        script->args_->push_float(kSpeed, static_cast<float>(get_speed(params, script)));
        return 1;
    case 0x68C86AEC: // the next play's blend period
        params->fetch_float(0u, &next_blend_, 0);
        has_next_blend_ = true;
        return 1;
    case 0x6ECEB234: // whether it's flipped
        return static_cast<int>((flag_bits_ >> 29) & 1);
    case 0x74CF5C94: // wait_anim
        return wait_anim(params);
    case 0x80FDBDF2: // whether it cycles
        return is_cycling() ? 1 : 0;
    case 0x9069F357: {                                      // AnimExists (the anim set's unnamed checksum)
        auto anim = reinterpret_cast<uint32_t>(params);     // (its argument's place reused, as the original)
        params->read_hash_value(0u, &anim, 0);
        return has_value(anim) ? 1 : 0;
    }
    case 0x922E7D14: // whether the unnamed time (frames) was passed since the last frame
        return time_passed(params);
    case 0x986D274E: // to every timeline
        for (const memory::ListNode* node = timelines_.next; !is_head(node) && node; node = node->next)
            static_cast<AnimTrack*>(node->data)->invoke_method_by_name(name, params, script);
        return 1;
    case 0x98A32669: {                                                       // flag4C from enabled
        int enabled = static_cast<int>(reinterpret_cast<uintptr_t>(params)); // (the argument's place, as above)
        params->fetch_int_value(0xAF06447B, &enabled, 1);
        flag_4c_ = enabled != 0;
        return 1;
    }
    case 0x1A0F9646: // AnimEquals
    case 0xB5FB7EB6:
        return is_anim_one_of(params, script) ? 1 : 0;
    case 0xC68365EB: // anim (Default when there's none) to timeline Index's (the playing one's) controllers
        return pass_anim_to_timeline(params, script);
    case 0xEA6D0EFD:
        return 1;
    case 0x5E3F0F17:
    case 0x6AAEB76F:
    case 0x83654874:
    case 0xBD4EDD44: // SetPartialAnimSpeed
    case 0xD63A1B81: // GetPartialAnimParams
    case 0xF5E2B871: {
        const memory::ListNode* node = timelines_.next;
        if (is_head(node) || !node)
            return 1;
        return static_cast<AnimTrack*>(node->data)->invoke_method_by_name(name, params, script) != 0 ? 1 : 0;
    }
    default:
        return 2;
    }
}

// The wait_anim member function (0x0047FDCE): the script waits until the playing timeline reaches a time - in
// frames (Frame / Frames), a percentage of its range (Percent), seconds (0x49E0EE96 / Seconds) or milliseconds -
// from the current time (0x91A4C826), the end (FromEnd) or its start; the object's script made if it has none.
int BlendChannel::wait_anim(Struct* params)
{
    float start = 0.0f, current = 0.0f, end = 0.0f;
    objects::get_channel_times(this, &start, &current, &end);
    float time = 0.0f;
    params->fetch_float(0u, &time, 0);
    if (params->has_bare_flag(0x4A07C332) || params->has_bare_flag(0x019176C5)) // Frame, Frames
        time = time * kSecondsPerFrame;
    else if (params->has_bare_flag(0x9E497FC6)) // Percent
        time = static_cast<float>((static_cast<double>(end) - start) * time * static_cast<double>(0.01f) + start);
    else if (!params->has_bare_flag(0x49E0EE96) && !params->has_bare_flag(0xD029F619)) // (Seconds: as it is)
        time = time * 0.001f;
    const bool forwards = start <= end;
    // (x87: rounded once to a float; a NaN kept as it is - not negated)
    auto sum = [](float a, float b) { return static_cast<float>(static_cast<double>(a) + b); };
    auto difference = [](float a, float b) { return static_cast<float>(static_cast<double>(a) - b); };
    if (params->has_bare_flag(0x91A4C826))
        wait_time_ = forwards ? sum(time, current) : difference(current, time);
    else if (params->has_bare_flag(0xE18CD075)) // FromEnd
        wait_time_ = forwards ? difference(end, time) : sum(time, start);
    else
        wait_time_ = time;
    if (forwards ? wait_time_ > end : wait_time_ < end)
        wait_time_ = end;
    objects::GameEntity* owner = get_owner(this);
    if (!owner->own_script_) {
        owner->own_script_ = script::Script::create();
    }
    if ((start <= end && current >= wait_time_) || (start >= end && current <= wait_time_)) {
        owner->own_script_->stop_waiting(); // (already there)
        waiting_ = false;
    } else {
        owner->own_script_->gate_off(0);
        waiting_ = true;
    }
    return 1;
}

// The member function 0x922E7D14 (0x004800D3): whether the time given (the unnamed float, in frames, from the
// start) lies between the playing timeline's time last frame and now - either way round.
int BlendChannel::time_passed(Struct* params)
{
    float start = 0.0f, current = 0.0f, end = 0.0f;
    objects::get_channel_times(this, &start, &current, &end);
    float time = 0.0f;
    params->fetch_float(0u, &time, 0);
    time = time * kSecondsPerFrame;
    auto now = static_cast<float>(static_cast<double>(current) - start);
    auto before = static_cast<float>(static_cast<double>(prev_timeline_pos_) - start);
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
int BlendChannel::pass_anim_to_timeline(Struct* params, script::Script* script)
{
    auto anim = reinterpret_cast<uint32_t>(params); // (the argument's place, as above)
    params->read_hash_value(kAnim, &anim, 0);
    if (!has_value(anim)) {
        anim = kDefault;
        params->put_name_key(kAnim, kDefault);
    }
    params->push_float(0x79A07F3F, fetch_value(anim)); // Duration
    int index = 0;
    AnimTrack* timeline = masked(timelines_.next);
    if (params->fetch_int_value(kIndex, &index, 0)) {
        for (; timeline && index; --index)
            timeline = masked(elem(timeline)->next);
    }
    if (!timeline)
        return 0;
    if (timeline == masked(timelines_.next)) {
        clear_flag();
        set_anim_tags(anim);
    }
    return timeline->invoke_method_by_name(0xC68365EBu, params, script) != 0 ? 1 : 0;
}

} // namespace gfx

namespace gfx {

namespace {

// 64 quaternions nothing reads (only their static initialiser writes them).
float g_spare_rotations[256];

} // namespace

// 0x00640AA0 (a static initialiser): each (0, 0, 0, 1).
void construct_spare_rotations()
{
    for (int i = 0; i < 64; ++i) {
        g_spare_rotations[4 * i + 0] = 0.0f;
        g_spare_rotations[4 * i + 1] = 0.0f;
        g_spare_rotations[4 * i + 2] = 0.0f;
        g_spare_rotations[4 * i + 3] = 1.0f;
    }
}

} // namespace gfx
