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
#include <array>
#include <span>

#include "wire/message_ids.h"
#include "base/core/dbg.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "engine/memory/slab_bank.h"
#include "engine/lists/chain_start.h"
#include "engine/gfx/anim_track.h"
#include "engine/gfx/frame_source.h"
#include "engine/gfx/model_utils.h"
#include "engine/gfx/rig.h"
#include "base/maths/maths.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/net/player_info.h"
#include "wire/skater_messages.h"
#include "formats/byte_stream.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/cutscene.h"
#include "engine/objects/movie_manager.h"
#include "engine/objects/entity.h"
#include "engine/objects/skeleton_component.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/sys/startup_init.h"
#include "engine/memory/arena_alloc.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/engine_parts.h"
#include "engine/net/net_game_state.h"
#include "base/core/checksums.h"
#include <gsl/pointers>
#include <gsl/assert>

namespace gfx {

// Text unknown for the checksums named by use (script method / parameter names).
constexpr uint8_t kNoWobbleValue = 0xFF;                    // the "last sent" wobble values before anything was sent
constexpr int kParamBagBytes = 8;
constexpr int kBoneCountMask = 0x0FFFFFFF;
constexpr int kBoneRecordBytes = 16;
constexpr int kFlippedFlagBit = 29;                         // flag_bits_: the animation plays flipped
constexpr double kByteMaxDouble = 255.0;
constexpr uint32_t kWobbleGoal = 0x4D747FA0U;       // text unknown (the wobble goal's parameter)
constexpr uint32_t kChooseWobbleGoal = 0xD0209498U; // choose_wobble_goal
constexpr float kWobbleBaseStepSlow = 0.05F;        // the known values of the first wobble detail
constexpr float kWobbleBaseStepMedium = 0.1F;
constexpr float kWobbleBaseStepFast = 10.1F;
constexpr int kWobbleLastDetailShift = 5;                   // the fifth detail's bit in the wobble message
constexpr float kSpeedSameTolerance = 0.05F;
constexpr int kAnimScratchSourceLine = 0x963;               // the line of the original's size report
constexpr int kAnimScratchSubsetBytes = 0x840;
constexpr int kAnimScratchHeaderBytes = 0x90;
constexpr int kAnimScratchBlendBytes = 0x848;
constexpr float kPlayToEnd = 1000.0F;           // an end time no animation reaches
constexpr uint32_t kEventsOnFlag = 0xE1B3F630U; // text unknown (the bare flag that turns events on)
constexpr float kPercentToFraction = 0.01F;
constexpr float kMillisecondsToSeconds = 0.001F;
constexpr uint32_t kDurationKey = 0x23272E4DU;        // text unknown (the duration parameter, in seconds)
constexpr uint32_t kPartialAnimId = 0xF0CE9E0FU;      // text unknown (the partial controller's id)
constexpr uint32_t kTo = 0x28782D3BU;                 // To
constexpr uint32_t kTimelineInfoMethod = 0x1DF59A54U; // text unknown (the info of a timeline to the script)
constexpr uint32_t kIsClipDoneMethod = 0x2889C6C9U;   // text unknown (whether the clip is complete)
constexpr uint32_t kSetRangeDurationMethod =
    0x4FAF61A9U; // text unknown (every timeline's speed so its range takes a duration)
constexpr uint32_t kIsCyclingMethod = 0x80FDBDF2U;          // text unknown (whether it cycles)
constexpr uint32_t kPassAnimToTimelineMethod = 0xC68365EBU; // text unknown (an anim to the timeline's controllers)
constexpr uint32_t kTimelineForwardMethodA = 0x5E3F0F17U;   // text unknown (passed on to the playing timeline)
constexpr uint32_t kTimelineForwardMethodB = 0x6AAEB76FU;   // text unknown (passed on to the playing timeline)
constexpr uint32_t kTimelineForwardMethodC = 0xF5E2B871U;   // text unknown (passed on to the playing timeline)
constexpr std::size_t kSpareRotationFloats = 256;
constexpr int kSpareRotationCount = 64;

// Checksums used in this file (the text each hashes).
constexpr uint32_t kAddAnimController = 0x83654874U;             // AddAnimController
constexpr uint32_t kAnimEquals = 0x1A0F9646U;                    // AnimEquals
constexpr uint32_t kAnimEventTableName = 0x5B8C6DC2U;            // AnimEventTableName
constexpr uint32_t kAnimExists = 0x9069F357U;                    // AnimExists
constexpr uint32_t kAnimFinished = 0x76CC99D5U;                  // AnimFinished
constexpr uint32_t kAnimName = 0x6C2BFB7FU;                      // AnimName
constexpr uint32_t kAnimTagTable = 0x95807202U;                  // AnimTagTable
constexpr uint32_t kBlendPeriod = 0x8F0D24EDU;                   // BlendPeriod
constexpr uint32_t kBlendPeriodOut = 0x68C86AECU;                // BlendPeriodOut
constexpr uint32_t kBlendWeight = 0x4D786F62U;                   // BlendWeight
constexpr uint32_t kBlendperiodpercent = 0x4F3ABD6CU;            // blendperiodpercent
constexpr uint32_t kDontInterrupt = 0x84F13067U;                 // DontInterrupt
constexpr uint32_t kDuration = 0x79A07F3FU;                      // Duration
constexpr uint32_t kEffectivePreviousAnimDuration = 0x4B4A153AU; // EffectivePreviousAnimDuration
constexpr uint32_t kEnableBlending = 0xCF466AA2U;                // EnableBlending
constexpr uint32_t kEnabled = 0xAF06447BU;                       // Enabled
constexpr uint32_t kFRAME = 0x4A07C332U;                         // FRAME
constexpr uint32_t kFROM = 0x46E55E8FU;                          // FROM
constexpr uint32_t kFrameIs = 0x922E7D14U;                       // FrameIs
constexpr uint32_t kFrames = 0x019176C5U;                        // Frames
constexpr uint32_t kFromEnd = 0xE18CD075U;                       // FromEnd
constexpr uint32_t kGetAnimLength = 0x42FFC3CEU;                 // GetAnimLength
constexpr uint32_t kGetPartialAnimParams = 0xD63A1B81U;          // GetPartialAnimParams
constexpr uint32_t kLength = 0xFE82614DU;                        // Length
constexpr uint32_t kNoRestart = 0xFE09FE09U;                     // NoRestart
constexpr uint32_t kOBJEnableAnimBlending = 0x98A32669U;         // OBJ_EnableAnimBlending
constexpr uint32_t kOFF = 0xD443A2BCU;                           // OFF
constexpr uint32_t kObjAnimEquals = 0xB5FB7EB6U;                 // Obj_AnimEquals
constexpr uint32_t kObjAnimationFlipped = 0x6ECEB234U;           // Obj_AnimationFlipped
constexpr uint32_t kObjFlip = 0x27A98022U;                       // Obj_Flip
constexpr uint32_t kObjGetAnimSpeed = 0x5195BCBBU;               // Obj_GetAnimSpeed
constexpr uint32_t kObjSetAnimCycleMode = 0x58C52F64U;           // Obj_SetAnimCycleMode
constexpr uint32_t kPartialAnim = 0x659BF355U;                   // PartialAnim
constexpr uint32_t kPartialAnimOverlay = 0x6C565F57U;            // PartialAnimOverlay
constexpr uint32_t kPercent = 0x9E497FC6U;                       // Percent
constexpr uint32_t kReferenceChecksum = 0x52A864B0U;             // ReferenceChecksum
constexpr uint32_t kRelative = 0x91A4C826U;                      // relative
constexpr uint32_t kRemoveAnimController = 0x986D274EU;          // RemoveAnimController
constexpr uint32_t kSTAGE = 0x3D836C96U;                         // STAGE
constexpr uint32_t kSecond = 0x49E0EE96U;                        // Second
constexpr uint32_t kSeconds = 0xD029F619U;                       // Seconds
constexpr uint32_t kSetPartialAnimSpeed = 0xBD4EDD44U;           // SetPartialAnimSpeed
constexpr uint32_t kSetWobbleDetails = 0xEA6D0EFDU;              // SetWobbleDetails
constexpr uint32_t kSkeletonName = 0x09794932U;                  // SkeletonName
constexpr uint32_t kSpazFactor = 0xF90B0824U;                    // SpazFactor
constexpr uint32_t kSyncToPreviousAnim = 0x099371AEU;            // SyncToPreviousAnim
constexpr uint32_t kSyncToReversePreviousAnim = 0xFF5AC79EU;     // SyncToReversePreviousAnim
constexpr uint32_t kTYPE = 0x7321A8D6U;                          // TYPE
constexpr uint32_t kWaitAnim = 0x74CF5C94U;                      // WaitAnim
constexpr uint32_t kWobbleAmpA = 0xFD266A26U;                    // WobbleAmpA
constexpr uint32_t kWobbleAmpB = 0x642F3B9CU;                    // WobbleAmpB
constexpr uint32_t kWobbleK1 = 0x0F43FD49U;                      // WobbleK1
constexpr uint32_t kWobbleK2 = 0x964AACF3U;                      // WobbleK2

namespace {

// NOT ORIGINAL grouping - the state the original kept in globals. Made on first use, never destroyed (blend_scratch()).
struct BlendScratch {
    std::span<Stance> blend_buffer_;
    std::span<float> blend_weights_;
    std::span<uint32_t> blend_sets_;
};

BlendScratch& blend_scratch()
{
    static core::NoDestroy<BlendScratch> state;
    return state.get();
}

} // namespace

std::span<Stance>& blend_buffer() { return blend_scratch().blend_buffer_; }
std::span<float>& blend_weights() { return blend_scratch().blend_weights_; }
std::span<uint32_t>& blend_sets() { return blend_scratch().blend_sets_; }

namespace {

using script::Struct;

constexpr uint32_t kSkeleton = 0x222756D5;    // skeleton
constexpr uint32_t kAnimScratch = 0x7C0B34B4; // AnimScratch
constexpr uint32_t kFlipped = 0x20000000;
constexpr float kFramesPerSecond = 60.0F;        // 0x00645EA0
constexpr float kSecondsPerFrame = 1.0F / 60.0F; // 0x006463B8

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
constexpr float kStockCrossfade = 0.3F;                                                 // 0x3E99999A
constexpr float kFixedPoint = 4096.0F;                                                  // 0x00648730

// The network messages a channel sends (wire/skater_messages.h).
using online::MessageId;

// NOT ORIGINAL grouping - anim_channel.cpp's state (the original's globals). Made on first use, never destroyed
// (anim_channel_state()).
struct AnimChannelState {
    uint8_t last_wobble_target_{kNoWobbleValue};
    uint8_t last_wobble_details_{kNoWobbleValue};
    int8_t last_loop_type_{-1};
    std::span<Stance> poses_;
    int max_blends_{};
    int subsets_{};
};

AnimChannelState& anim_channel_state()
{
    static core::NoDestroy<AnimChannelState> state;
    return state.get();
}

// What was last sent (all start 0xFF).
uint8_t& last_wobble_target() { return anim_channel_state().last_wobble_target_; }
uint8_t& last_wobble_details() { return anim_channel_state().last_wobble_details_; }
int8_t& last_loop_type() { return anim_channel_state().last_loop_type_; }

// The blend buffers (init_buffers).

std::span<Stance>& current_poses() { return anim_channel_state().poses_; }
int& max_blends() { return anim_channel_state().max_blends_; }
int& get_subsets() { return anim_channel_state().subsets_; }

bool is_head(const memory::ListNode* node) { return node->data() == memory::ListNode::head_marker(); }
// A list node as the original masks it: null when it's the head.
// A timeline's list node holds the timeline (its data).
AnimTrack* track_of(memory::ListNode* node) { return core::down_cast<AnimTrack>(static_cast<AnimTimeline*>(node->data())); }
AnimTrack* masked(memory::ListNode* node) { return is_head(node) ? nullptr : track_of(node); }
// A timeline's list node; none for no timeline (the node is the timeline's first member: the original's address of
// it through a null timeline was null, which ends the walks below).
memory::ListNode* elem(AnimTrack* timeline) { return timeline != nullptr ? &timeline->list_link() : nullptr; }

// The playing timeline (the list's first), or null.
AnimTrack* get_first(BlendChannel* channel)
{
    memory::ListNode* node = channel->timelines().next();
    return is_head(node) || !node ? nullptr : track_of(node);
}

void uncouple(memory::ListNode* node)
{
    node->prev()->set_next(node->next());
    node->next()->set_prev(node->prev());
    node->set_next(node);
    node->set_prev(node);
}

// A timeline (an AnimTrack, its own node) deleted: its scalar deleting destructor (the original's slot 0).
void delete_timeline(memory::ListNode* node) { static_cast<AnimTrack*>(node->data())->teardown_track(1); }
// Every timeline of the list deleted (0x0057A2F0 over the list).
void delete_all_timelines(memory::ListNode* head)
{
    lists::delete_each<AnimTrack>(head, [](AnimTrack* timeline) { timeline->teardown_track(1); });
}

objects::GameEntity* get_owner(BlendChannel* channel)
{
    return channel->owner_ptr();
}

// The network manager (0x007CCE14) and this machine's client app for the channel's object - null when the object
// isn't this machine's player's skater.
online::PlayerRecord* find_own_player_record(uint32_t object_id)
{
    auto* player = (online::online_hub()->find_player_slot_by_obj(static_cast<uint16_t>(object_id)));
    if (!player || (player->is_own_player() == 0)) {
        return nullptr;
    }
    return player;
}
net::Guest* client_of(online::PlayerRecord* player)
{
    return core::down_cast<net::Guest>(online::online_hub()->fetch_patron(static_cast<int>(player->get_skater_view_slot())));
}

void post_out(net::Guest* app, net::NetMessageInfo& message, online::MessageId id, void* data, uint16_t length, bool singular)
{
    message.set_content(data);
    message.set_byte_count(length);
    message.set_ident(id);
    if (singular) {
        message.set_singular(true);
    }
    app->send_to_host(&message);
}

// A structure made and freed as the original's inlined new / delete do.
gsl::owner<Struct*> spawn_param_bag()
{
    void* memory = script::struct_pools().carve(kParamBagBytes);
    return memory ? memory::construct_in<script::Struct>(memory) : nullptr;
}

// The playing timeline's controllers given a member function with `params` (slot 5, no script).
void pass_to_first(BlendChannel* channel, uint32_t name, Struct* params)
{
    masked(channel->timelines().next())->invoke_method_by_name(name, params, nullptr);
}

// The original's _ftol: truncated towards zero.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The track's fade (start_fade 0x00481B40), weight (0x00481B80, ST0) and operator new (0x00481EE0; the original
// passes it 0 too).
bool start_blend_out(AnimTrack* timeline, float period) { return timeline->start_fade(period); }
double current_weight(AnimTrack* timeline) { return timeline->get_weight(); }
void* new_timeline(uint32_t size) { return AnimTrack::operator new(size); }

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
    return (core::down_cast<objects::BoneRigModule>(component)->bone_rig());
}

} // namespace

// 0x0047DD10
int count_list_items(const memory::ListNode* head)
{
    const memory::ListNode* node = head->next();
    int count = 0;
    while (!is_head(node) && node) {
        node = node->next();
        ++count;
    }
    return count;
}

// --- making and destroying ---

// 0x0047F150
BlendChannel::BlendChannel(objects::GameEntity* object, objects::AnimBlenderComponent* blender, bool full)
    : owner_ptr_(object), blender_(blender)
{
    list_link_.make_node(this);
    timelines_.make_head();
    const float value = full ? 1.0F : 0.0F;
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
    if ((flags & 1) != 0) {
        memory::dealloc(this);
    }
    return this;
}

// 0x0047EF20
void BlendChannel::get_reset()
{
    delete_all_timelines(&timelines_);
    const bool waiting = waiting_;
    has_next_blend_ = false;
    next_blend_ = 0.0F;
    if (waiting) {
        waiting_ = false;
        if (script::Script* script = get_owner(this)->own_script()) {
            script->stop_waiting();
        }
    }
    wait_time_ = 0.0F;
    prev_timeline_pos_ = 0.0F;
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
    if (blend && blender_->model_ready()) {
        return blend_pose(index);
    }
    return false;
}

// 0x0047EF60
void BlendChannel::release_waiting_script(script::Script* script)
{
    if (script->suspend_reason() != script::BlockReason::kAnimDone) {
        return;
    }
    if (const AnimTrack* first = get_first(this)) {
        if (first->is_accomplished()) {
            script->stop_waiting();
        }
    }
}

// 0x0047E030 (x87: the blend's time left unrounded through the division)
void BlendChannel::update_blend()
{
    if (const AnimTrack* first = get_first(this)) {
        prev_timeline_pos_ = first->ongoing();
    }
    if (period_left_ > 0.0F) {
        double left = period_left_ - frame_clock::get_slomo_frame_seconds();
        period_left_ = static_cast<float>(left);
        if (!(left > 0.0)) {
            left = 0.0;
        }
        period_left_ = static_cast<float>(left);
        const double done = left / period_;
        ongoing_ = static_cast<float>(((static_cast<double>(outset_) - aim_point_) * done) + aim_point_);
    } else {
        ongoing_ = aim_point_;
    }
    if (!waiting_) {
        return;
    }
    objects::GameEntity* owner = get_owner(this);
    if (!owner->own_script()) {
        owner->set_own_script(script::Script::create());
    }
    bool stop = false;
    if (!owner->own_script()->is_blocked(false)) {
        stop = true;
    } else {
        float start = 0.0F;
        float current = 0.0F;
        float end = 0.0F;
        objects::get_channel_times(this, &start, &current, &end);
        // (forwards: the time reached; backwards - or a range of none - reached the other way; NaN: never)
        if (start <= end && current >= wait_time_) {
            stop = true;
        } else {
            stop = start >= end && current <= wait_time_;
        }
    }
    if (stop) {
        owner->own_script()->stop_waiting();
        waiting_ = false;
    }
}

// 0x0047E1B0
void BlendChannel::update_timelines()
{
    int count = count_list_items(&timelines_);
    AnimTrack* timeline = masked(timelines_.next());
    for (; count > 0; --count) {
        timeline->tick_track();
        timeline = masked(elem(timeline)->next());
    }
}

// 0x0047DDC0
void BlendChannel::remove_done_timelines()
{
    memory::ListNode* node = timelines_.next();
    if (is_head(node) || !node) {
        return;
    }
    while (node) {
        AnimTrack* next = masked(node->next());
        if (!track_of(node)->is_operative()) {
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
    for (AnimTrack* timeline = masked(timelines_.next()); timeline;) {
        AnimTrack* next = masked(elem(timeline)->next());
        if (++count >= max_blends()) {
            uncouple(elem(timeline));
            delete_timeline(elem(timeline));
        }
        timeline = next;
    }
    if (count_list_items(&timelines_) != 0) {
        AnimTrack* first = get_first(this);
        if (first && start_blend_out(first, period)) {
            float others = 0.0F;
            for (memory::ListNode* node = elem(first)->next(); !is_head(node) && node; node = node->next()) {
                auto* timeline = track_of(node);
                if (timeline->is_fading()) {
                    others = static_cast<float>((static_cast<double>(timeline->fade_scale()) * timeline->fade_left()) +
                                                others);
                }
            }
            first->set_fade_scale(static_cast<float>((1.0 - others) / first->fade_left()));
        }
    }
    void* memory = new_timeline(sizeof(AnimTrack));
    AnimTrack* timeline = memory ? memory::construct_global_in<AnimTrack>(memory, get_owner(this)) : nullptr;
    memory::ListNode* node = elem(timeline); // (null: a crash, as the original)
    node->set_prev(&timelines_);
    node->set_next(timelines_.next());
    timelines_.next()->set_prev(node);
    timelines_.set_next(node);
}

// 0x0047ED60
bool BlendChannel::blend_pose(int index)
{
    if (render::engine_parts().suppress_draw() || (lookup_key_ == 0U)) {
        return false;
    }
    Rig* skeleton = skeleton_of(bone_rig_);
    if (!skeleton) {
        return false;
    }
    skeleton->set_lod(blender_->distance_squared());
    const int count = count_list_items(&timelines_);
    const RigData* data = skeleton->payload();
    for (int i = 0; i < count; ++i) {
        // (each pose's translations start as the skeleton's bind translations)
        Stance& pose = core::at(current_poses(), i);
        std::memcpy(pose.translations.data(), data->bind_translations(),
                    static_cast<size_t>(skeleton->get_num_bones() & kBoneCountMask) * kBoneRecordBytes);
        fetch_pose(i, &pose, &core::at(blend_weights(), i));
    }
    if (count > 1) {
        mix_poses(current_poses(), blend_weights(), skeleton->get_num_bones(), core::at(current_poses(), 0), count);
    }
    core::at(blend_buffer(), index) = core::at(current_poses(), 0);
    return true;
}

// 0x0047EFF0
bool BlendChannel::blend_first_pose() { return blend_pose(0); }

// 0x0047EAA0
void BlendChannel::fetch_pose(int index, Stance* pose, float* weight)
{
    AnimTrack* timeline = nullptr;
    memory::ListNode* node = timelines_.next();
    if (!is_head(node) && node) {
        for (;;) {
            if (index-- == 0) {
                timeline = track_of(node);
                break;
            }
            node = node->next();
            if (is_head(node) || !node) {
                break;
            }
        }
    }
    const double w = current_weight(timeline); // (null when not found: a crash, as the original)
    auto rounded = static_cast<float>(w);
    if (!(w <= 0.0)) {
        skeleton_of(bone_rig_); // (read and not used)
        timeline->get_poses(pose);
        *weight = rounded;
    } else {
        *weight = 0.0F;
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
            std::array<uint8_t, 2> data = {{static_cast<uint8_t>(object_id), static_cast<uint8_t>(flip)}};
            post_out(app, message, MessageId::kFlip, data.data(), 2, false);
        }
    }
    const bool was = ((flag_bits_ >> 29) & 1) != 0U;
    flag_bits_ = (flag_bits_ & ~kFlipped) | (flip ? kFlipped : 0);
    return was;
}

// 0x0047DD80
void BlendChannel::toggle_flipped()
{
    const net::AppHost* app = online::online_hub()->fetch_patron(0);
    const uint32_t unused = app->clock();
    set_flipped(get_owner(this)->ident(), ((flag_bits_ >> kFlippedFlagBit) & 1) == 0U, unused, true);
}

// 0x0047E010
void BlendChannel::advance_clock(float frames)
{
    memory::ListNode* node = timelines_.next();
    if (!is_head(node) && node) {
        track_of(node)->advance_clock(frames);
    }
}

// 0x0047E210
bool BlendChannel::is_clip_done()
{
    const AnimTrack* first = get_first(this);
    return first ? first->is_accomplished() : false;
}

// 0x0047E240
bool BlendChannel::is_cycling()
{
    const AnimTrack* first = get_first(this);
    if (!first) {
        return false;
    }
    return first->repeat_mode() == AnimTimeline::kCycle || first->repeat_mode() == AnimTimeline::kPingPong;
}

// 0x0047DF50
bool BlendChannel::is_anim_one_of(Struct* params, script::Script* /*script*/)
{
    const AnimTrack* first = get_first(this);
    const uint32_t anim = first ? first->anim() : 0;
    for (script::Component* component = params->step_to_entry(nullptr); component;
         component = params->step_to_entry(component)) {
        if (component->name != 0U) {
            continue;
        }
        script::Array* array = nullptr;
        if (value_kind(*component) == script::QbValueKind::kName) {
            const script::SymbolTableEntry* symbol = script::resolve_symbol(component->value.word());
            if (symbol && symbol->type() == script::QbValueKind::kArray) {
                array = symbol->value().array();
            } else {
                if (component->value.word() == anim) {
                    return true;
                }
                continue;
            }
        } else if (value_kind(*component) == script::QbValueKind::kArray) {
            array = component->value.array();
        } else {
            continue;
        }
        if (array) {
            for (uint32_t i = 0; i < array->extent(); ++i) {
                if (array->fetch_part(i) == anim) {
                    return true;
                }
            }
        }
    }
    return false;
}

// 0x0047E280
void BlendChannel::choose_wobble_goal(float target, bool send)
{
    if (send && ident_ == 0) {
        if (online::PlayerRecord* player = find_own_player_record(get_owner(this)->ident())) {
            net::Guest* app = client_of(player);
            if (target < 0.0F) {
                target = 0.0F;
            } else if (target > 1.0F) {
                target = 1.0F;
            }
            std::array<uint8_t, 2> data{};
            core::at(data, 1) = static_cast<uint8_t>(get_truncated(target * kByteMaxDouble)); // (0x00646B88)
            core::at(data, 0) = static_cast<uint8_t>(get_owner(this)->ident());
            if (last_wobble_target() != core::at(data, 1)) {
                net::NetMessageInfo message;
                post_out(app, message, MessageId::kWobbleTarget, data.data(), 2, true);
                last_wobble_target() = core::at(data, 1);
            }
        }
    }
    if (!get_first(this)) {
        return;
    }
    gsl::owner<Struct*> params = spawn_param_bag();
    params->push_float(checksums::Name{kWobbleGoal}, target);
    pass_to_first(this, kChooseWobbleGoal, params); // choose_wobble_goal
    script::free_struct(params);
}

// 0x0047E450
void BlendChannel::set_wobble_details(std::span<const float, kWobbleDetailCount> details, bool send)
{
    if (send && ident_ == 0) {
        if (online::PlayerRecord* player = find_own_player_record(get_owner(this)->ident())) {
            net::Guest* app = client_of(player);
            // Which of its known values each detail has (0x00645D6C... - the server sends those).
            uint8_t bits = 0;
            if (core::at(details, 0) == kWobbleBaseStepSlow) {
                bits = 0;
            } else if (core::at(details, 0) == kWobbleBaseStepMedium) {
                bits = 1;
            } else if (core::at(details, 0) == kWobbleBaseStepFast) {
                bits = 2;
            }
            // (each detail's bit: 1 for its second setting; its first - 0.04, 0.0022, 0.0017, 1.5 - and any other value 0)
            const uint8_t b1 = core::at(details, 1) == 10.1F ? 1 : 0;
            const uint8_t b2 = core::at(details, 2) == 20.0F ? 1 : 0;
            const uint8_t b3 = core::at(details, 3) == 10.0F ? 1 : 0;
            const uint8_t b4 = core::at(details, 4) == 1.0F ? 1 : 0;
            std::array<uint8_t, 2> data{};
            core::at(data, 0) = static_cast<uint8_t>(bits | b1 << 2 | b2 << 3 | b3 << 4 | b4 << kWobbleLastDetailShift);
            core::at(data, 1) = static_cast<uint8_t>(get_owner(this)->ident());
            if (last_wobble_details() != core::at(data, 0)) {
                net::NetMessageInfo message;
                post_out(app, message, MessageId::kWobbleDetails, data.data(), 2, true);
                last_wobble_details() = core::at(data, 0);
            }
        }
    }
    if (!get_first(this)) {
        return;
    }
    gsl::owner<Struct*> params = spawn_param_bag();
    params->push_float(checksums::Name{kWobbleAmpA}, core::at(details, 0)); // wobbleAmpA
    params->push_float(checksums::Name{kWobbleAmpB}, core::at(details, 1)); // wobbleAmpB
    params->push_float(checksums::Name{kWobbleK1}, core::at(details, 2)); // wobbleK1
    params->push_float(checksums::Name{kWobbleK2}, core::at(details, 3)); // wobbleK2
    params->push_float(checksums::Name{kSpazFactor}, core::at(details, 4)); // spazFactor
    pass_to_first(this, kSetWobbleDetails, params);    // SetWobbleDetails
    script::free_struct(params);
}

// 0x0047E740
void BlendChannel::change_repeat_mode(int loop_type, bool send)
{
    if (send && ident_ == 0) {
        if (online::PlayerRecord* player = find_own_player_record(get_owner(this)->ident())) {
            net::Guest* app = client_of(player);
            std::array<uint8_t, 2> data = {{static_cast<uint8_t>(get_owner(this)->ident()), static_cast<uint8_t>(loop_type)}};
            if (last_loop_type() != loop_type) {
                net::NetMessageInfo message;
                post_out(app, message, MessageId::kLoopType, data.data(), 2, false);
                last_loop_type() = static_cast<int8_t>(loop_type);
            }
        }
    }
    if (AnimTrack* first = get_first(this)) {
        first->set_repeat_mode(loop_type);
    }
}

// 0x0047E810
void BlendChannel::get_reverse(uint32_t /*unused*/)
{
    if (AnimTrack* first = get_first(this)) {
        first->set_direction(first->direction() != 0 ? 0 : -1);
    }
}

// 0x0047E850 (x87: the range and the ratio's products unrounded)
void BlendChannel::set_speed(float speed, bool send, bool all, bool keep_ratio)
{
    const AnimTrack* first = get_first(this);
    if (!first) {
        return;
    }
    if (!all && std::abs((static_cast<double>(first->speed()) * kFramesPerSecond) - speed) <
                    kSpeedSameTolerance) { // (0x00645D6C)
        return;
    }
    if (send && ident_ == 0) {
        if (online::PlayerRecord* player = find_own_player_record(get_owner(this)->ident())) {
            if (net::Guest* app = client_of(player)) {
                net::NetMessageInfo message;
                online::AnimSpeedMessage sent;
                sent.speed_bits = std::bit_cast<uint32_t>(speed);
                sent.object_id = static_cast<uint8_t>(get_owner(this)->ident());
                std::array<uint8_t, online::kAnimSpeedSize> data{};
                util::ByteWriter writer(data.data(), sizeof(data));
                online::write_anim_speed(writer, sent);
                post_out(app, message, online::MessageId::kAnimSpeed, data.data(), sizeof(data), false);
            }
        }
    }
    if (!all) {
        get_first(this)->set_speed(speed * kSecondsPerFrame);
        return;
    }
    if (keep_ratio) {
        float start = 0.0F;
        float current = 0.0F;
        float end = 0.0F;
        get_first(this)->get_times(&start, &current, &end);
        auto ratio = static_cast<float>(speed / (static_cast<double>(end) - start));
        for (memory::ListNode* node = timelines_.next(); !is_head(node) && node; node = node->next()) {
            auto* timeline = track_of(node);
            timeline->get_times(&start, &current, &end);
            timeline->set_speed(static_cast<float>((static_cast<double>(end) - start) * ratio * kSecondsPerFrame));
        }
    } else {
        memory::ListNode* node = timelines_.next();
        if (is_head(node) || !node) {
            return;
        }
        const float per_frame = speed * kSecondsPerFrame;
        for (; !is_head(node) && node; node = node->next()) {
            track_of(node)->set_speed(per_frame);
        }
    }
}

// 0x0047EA40
double BlendChannel::get_speed(Struct* /*params*/, script::Script* /*script*/)
{
    const AnimTrack* first = get_first(this);
    if (!first) {
        return 0.0;
    }
    return static_cast<double>(first->speed()) * kFramesPerSecond;
}

// 0x0047EA80
void BlendChannel::set_events_on(bool on)
{
    if (AnimTrack* first = get_first(this)) {
        events_on_ = on;
        first->set_events_on(on);
    }
}

// 0x0047F000
void BlendChannel::set_anim_tags(uint32_t anim)
{
    Struct* all = script::read_sub_struct(kAnimTagTable);
    Struct* tags = nullptr;
    if (!all || !all->read_sub_struct(anim, &tags, 0)) {
        return;
    }
    gsl::owner<Struct*> params = spawn_param_bag();
    gsl::owner<Struct*> copy = spawn_param_bag();
    copy->append_structure_if_any(tags);
    params->append_struct_ref(kAnimTags, copy); // AnimTags
    get_owner(this)->set_tags_from_struct(params);
    script::free_struct(params);
}

// 0x0047EB30
void BlendChannel::init_buffers(BufferCounts buffer_counts)
{
    const auto blends = buffer_counts.blends;
    const auto subsets = buffer_counts.subsets;
    max_blends() = blends;
    get_subsets() = subsets;
    debug::printf("D:%s:%i: size = %i\n", "nimblendsubset.cpp", kAnimScratchSourceLine, 0);
    memory::Hub* manager = memory::Hub::get_pull();
    manager->create_labeled_zone(checksums::Name{kAnimScratch},
                                 (get_subsets() * kAnimScratchSubsetBytes) + kAnimScratchHeaderBytes +
                                     (max_blends() * kAnimScratchBlendBytes),
                                 "AnimScratch",
                                 0); // "AnimScratch"
    manager->enter_heap_scope(manager->fetch_heap_by_key(kAnimScratch, 1));
    // (each pose's rotations made the identity by the vector constructor iterator 0x00401020 with 0x00464340; its
    // translations' constructor 0x005902E0 does nothing)
    auto new_poses = [](int count) {
        auto* block = static_cast<Stance*>(memory::alloc(static_cast<uint32_t>(count) * sizeof(Stance)));
        const std::span<Stance> poses(block, block ? static_cast<size_t>(count) : 0);
        for (Stance& pose : poses) {
            for (auto& rotation : pose.rotations) {
                rotation = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F}; // (0x00590300: the identity)
            }
        }
        return poses;
    };
    // A block of `count` words as a run of them (none without memory).
    auto new_words = []<typename Word>(int count) {
        auto* block = static_cast<Word*>(memory::alloc(static_cast<uint32_t>(count) * 4));
        return std::span<Word>(block, block ? static_cast<size_t>(count) : 0);
    };
    blend_scratch().blend_buffer_ = new_poses(get_subsets());
    current_poses() = new_poses(max_blends());
    blend_scratch().blend_sets_ = new_words.operator()<uint32_t>(max_blends());
    blend_scratch().blend_weights_ = new_words.operator()<float>(max_blends());
    memory::Hub::get_pull()->leave_heap_scope();
}

// --- playing ---

// 0x0047F260
void BlendChannel::play_anim(uint32_t anim, bool send, float from, float to, int loop_type, float blend_period,
                             float speed, Struct* params)
{
    if (anim == 0U) {
        anim = kDefault;
    }
    if (send && ident_ == 0) {
        if (online::PlayerRecord* player = find_own_player_record(get_owner(this)->ident())) {
            net::NetMessageInfo message;
            net::Guest* app = client_of(player);
            // loop type, object, anim, then the times, blend period and speed x 4096 as shorts
            online::PlayAnimMessage sent;
            sent.loop_type = static_cast<uint8_t>(loop_type);
            sent.object_id = static_cast<uint8_t>(get_owner(this)->ident());
            sent.anim = anim;
            sent.from = static_cast<uint16_t>(get_truncated(from * kFixedPoint));
            sent.to = static_cast<uint16_t>(get_truncated(to * kFixedPoint));
            sent.blend_period = static_cast<uint16_t>(get_truncated(blend_period * kFixedPoint));
            sent.speed = static_cast<uint16_t>(get_truncated(speed * kFixedPoint));
            std::array<uint8_t, online::kPlayAnimSize> data{};
            util::ByteWriter writer(data.data(), sizeof(data));
            online::write_play_anim(writer, sent);
            post_out(app, message, MessageId::kPlayAnim, data.data(), sizeof(data), false);
        }
    }
    if (blend_period == 0.0F || !flag_4c_) {
        delete_all_timelines(&timelines_);
    }
    push_timeline(blend_period);
    masked(timelines_.next())
        ->play_anim(anim, {.start = from, .end = to, .loop_type_ = loop_type, .blend_period_ = blend_period, .speed = speed}, ((flag_bits_ >> kFlippedFlagBit) & 1) != 0, params);
    if (Struct* tags = get_owner(this)->tag_table()) {
        tags->erase_module(kAnimTags);
    }
    set_anim_tags(anim);
}

// 0x004803D0
void BlendChannel::play_animation(uint32_t anim, float blend_period)
{
    play_anim(anim, false, 0.0F, *anim_length(lookup_key_ + anim), 0, blend_period, 1.0F, nullptr);
}

// 0x00480410
void BlendChannel::configure_from_data(Struct* params)
{
    params->read_hash_value(kSubsetId, &ident_, 0);
    params->fetch_float(kBlendWeight, &ongoing_, 0); // BlendWeight
    int priority = 0;
    params->fetch_int_value(kSTAGE, &priority, 0);
    rank_ = priority;
    params->read_hash_value(kAnimEventTableName, &event_table_, 0); // AnimEventTableName
    uint32_t anim_set = 0;                                 // (the original's is left as it was when neither is there)
    if (!params->read_hash_value(kReferenceChecksum, &anim_set, 0)) {
        params->read_hash_value(kSkeletonName, &anim_set, 1);
    }
    lookup_key_ = anim_set;
    play_anim(kDefault, false, 0.0F, kPlayToEnd, 0, kStockCrossfade, 1.0F, nullptr);
    get_reset();
    uint32_t anim = kDefault;
    const bool loop = params->read_hash_value(0xEAE64B47, &anim, 0);
    play_anim(anim, false, 0.0F, *anim_length(lookup_key_ + anim), 0, kStockCrossfade, 1.0F, nullptr);
    if (loop) {
        change_repeat_mode(AnimTimeline::kCycle, true);
    }
    if (params->has_bare_flag(kEnableBlending)) {
        flag_4c_ = true;
    }
    set_events_on(params->has_bare_flag(kEventsOnFlag));
}

// 0x0047F470 (x87: the synchronised start's fraction and product unrounded)
uint32_t BlendChannel::play_anim_from_script(Struct* params, script::Script* script, float blend_period)
{
    if (const AnimTrack* first = get_first(this)) {
        prev_timeline_pos_ = first->ongoing();
    }
    if (flag_4e_) {
        const AnimTrack* first = get_first(this);
        if (!first || !first->is_accomplished()) {
            return 0;
        }
    }
    if (lookup_key_ == 0U) {
        return 0;
    }
    uint32_t anim = 0;
    params->read_hash_value(kAnim, &anim, 0);
    if (anim == 0U) {
        script::dump_to_log(params, 0);
    }
    float speed = 1.0F;
    params->fetch_float(kSpeed, &speed, 0);
    const AnimTrack* first = get_first(this);
    const uint32_t playing = first ? first->anim() : 0;
    if (anim == kCurrent) {
        anim = playing;
    } else if (playing == anim && params->has_bare_flag(kNoRestart)) { // NoRestart
        return 0;
    }
    if (!anim_length(lookup_key_ + anim)) {
        anim = kDefault;
    }

    float blend = blend_period;
    if (has_next_blend_) {
        blend = next_blend_;
        has_next_blend_ = false;
    } else {
        float percent = 0.0F;
        if (params->fetch_float(kBlendperiodpercent, &percent, 0)) { // (of the anim's length)
            blend = static_cast<float>(percent * static_cast<double>(kPercentToFraction) * *anim_length(lookup_key_ + anim));
        } else {
            params->fetch_float(kBlendPeriod, &blend, 0); // BlendPeriod
        }
    }
    const float length = *anim_length(lookup_key_ + anim);
    float to = length;
    float from = 0.0F;
    float start = 0.0F; // the time the new one starts at
    bool sync = true;
    double fraction = 0.0;
    if (params->has_bare_flag(kSyncToPreviousAnim)) { // SyncToPreviousAnim: as far through as the playing one
        float current = 0.0F;
        float ends = 0.0F;
        objects::get_channel_times(this, &ends, &current, &ends);
        float playing_length = *anim_length(lookup_key_ + playing);
        params->fetch_float(kEffectivePreviousAnimDuration, &playing_length, 0); // (read and not used here)
        auto through = current / *anim_length(lookup_key_ + playing);
        if (through > 1.0F) {
            fraction = 1.0;
        } else if (through < 0.0F) {
            fraction = 0.0;
        } else {
            fraction = through;
        }
    } else if (params->has_bare_flag(kSyncToReversePreviousAnim)) { // as far from the end as the playing one
        float current = 0.0F;
        float ends = 0.0F;
        objects::get_channel_times(this, &ends, &current, &ends);
        float playing_length = *anim_length(lookup_key_ + playing);
        params->fetch_float(kEffectivePreviousAnimDuration, &playing_length, 0);
        fraction = maths::confine(static_cast<float>((static_cast<double>(playing_length) - current) / playing_length),
                                  0.0F, 1.0F);
    } else {
        sync = false;
        fraction = 0.0;
    }
    if (sync) {
        start = static_cast<float>(fraction * length);
        if (params->has_bare_flag(kBackwards)) {
            start = static_cast<float>(-(static_cast<double>(length) - start));
        }
        speed = static_cast<float>(get_speed(nullptr, nullptr));
    }
    float current = 0.0F;
    float ends = 0.0F;
    if (const AnimTrack* playing_now = get_first(this)) {
        playing_now->get_times(&ends, &current, &ends);
    }
    range_from_params(&from, &to, current, length, params, script);
    int loop_type = 0;
    loop_type_from_params(&loop_type, params);
    flag_4e_ = params->has_bare_flag(kDontInterrupt); // (wait for the playing one to complete)
    float duration = 0.0F;
    if (params->fetch_float(kDurationKey, &duration, 0)) {
        speed = static_cast<float>(length / duration);
    }
    play_anim(anim, true, from, to, loop_type, blend, speed, params);
    AnimTrack* timeline = masked(timelines_.next());
    if (start != 0.0F) {
        timeline->advance_clock(start);
    }
    timeline->set_events_on(events_on_);

    uint32_t partial = 0;
    if (params->read_hash_value(kPartialAnimOverlay, &partial, 0)) { // PartialAnimOverlay: a partial animation controller too
        gsl::owner<Struct*> controller = script::spawn_param_bag();
        if (controller) {
            controller = new (controller) script::Struct;
        }
        Expects(controller != nullptr);
        uint32_t id = 0; // (missing: 0 - the original read an argument's stack slot, an address no name has)
        if (params->read_hash_value(kPartialAnimId, &id, 0)) {
            controller->put_name_key(checksums::Name{kId}, id);
        }
        controller->put_name_key(checksums::Name{kTYPE}, kPartialAnim); // Type PartialAnim
        controller->put_name_key(checksums::Name{kAnimName}, partial);    // AnimName
        controller->push_float(checksums::Name{kFROM}, from);         // From
        controller->push_float(checksums::Name{kTo}, to);           // To
        controller->put_name_key(checksums::Name{0U}, kSeconds);    // Seconds
        controller->push_float(checksums::Name{kCurrent}, start);
        controller->push_float(checksums::Name{kSpeed}, speed);
        switch (loop_type) {
        case AnimTimeline::kCycle:
            controller->put_name_key(checksums::Name{0U}, kCycle);
            break;
        case AnimTimeline::kPingPong:
            controller->put_name_key(checksums::Name{0U}, kPingPong);
            break;
        case AnimTimeline::kHold:
            controller->put_name_key(checksums::Name{0U}, kHoldLoop);
            break;
        default:
            break;
        }
        attach_controller(masked(timelines_.next()), controller);
        script::free_struct(controller);
    }
    return anim;
}

// 0x0047FA00
int BlendChannel::invoke_method_by_name(uint32_t name, Struct* params, script::Script* script)
{
    switch (name) {
    case kTimelineInfoMethod: { // the info of timeline Index (0xD2D6A582; the playing one without it) to the script
        return add_timeline_info_from_script(params, script);
    }
    case kObjFlip: { // Flip: flipped, and every timeline's (or timeline Index's) controllers told
        return flip_from_script(name, params, script);
    }
    case kIsClipDoneMethod:
    case kAnimFinished: // (whether the animation is complete)
        return is_clip_done() ? 1 : 0;
    case kPlayAnim:
    case kObjPlayAnim: {
        play_anim_from_script(params, script, kStockCrossfade);
        memory::ListNode* node = timelines_.next();
        if (!is_head(node) && node && track_of(node)->repeat_mode() == AnimTimeline::kHold) {
            owner_ptr_
                ->invoke_method_by_name(kSetWobbleDetails, params,
                                        script); // (the object's own member function: the original's slot 3)
        }
        return 1;
    }
    case kGetAnimLength: { // GetAnimLength: of anim, to the script's length
        uint32_t anim = 0;
        params->read_hash_value(kAnim, &anim, 0);
        if (has_value(anim)) {
            script->args()->push_float(checksums::Name{kLength}, std::fabs(fetch_value(anim))); // length
        }
        return 1;
    }
    case kSetRangeDurationMethod: { // every timeline's speed so its range takes 0x23272E4D seconds
        float duration = 0.0F;
        params->fetch_float(kDurationKey, &duration, 1);
        for (memory::ListNode* node = timelines_.next(); !is_head(node) && node; node = node->next()) {
            auto* timeline = track_of(node);
            float start = 0.0F;
            float current = 0.0F;
            float end = 0.0F;
            timeline->get_times(&start, &current, &end);
            const float larger = start > end ? start : end;
            timeline->set_speed(static_cast<float>(larger / static_cast<double>(duration) * kSecondsPerFrame));
        }
        return 1;
    }
    case kObjSetAnimCycleMode: // the loop type: once with 0xD443A2BC, else cycling
        change_repeat_mode(params->has_bare_flag(kOFF) ? AnimTimeline::kOnce : AnimTimeline::kCycle, true);
        return 1;
    case kObjGetAnimSpeed: // the speed to the script's Speed
        script->args()->push_float(checksums::Name{kSpeed}, static_cast<float>(get_speed(params, script)));
        return 1;
    case kBlendPeriodOut: // the next play's blend period
        params->fetch_float(0U, &next_blend_, 0);
        has_next_blend_ = true;
        return 1;
    case kObjAnimationFlipped: // whether it's flipped
        return static_cast<int>((flag_bits_ >> kFlippedFlagBit) & 1);
    case kWaitAnim: // wait_anim
        return wait_anim(params);
    case kIsCyclingMethod: // whether it cycles
        return is_cycling() ? 1 : 0;
    case kAnimExists: {                                      // AnimExists (the anim set's unnamed checksum)
        uint32_t anim = 0; // (missing: 0 - the original read an argument's stack slot, an address no name has)
        params->read_hash_value(0U, &anim, 0);
        return has_value(anim) ? 1 : 0;
    }
    case kFrameIs: // whether the unnamed time (frames) was passed since the last frame
        return time_passed(params);
    case kRemoveAnimController: // to every timeline
        for (const memory::ListNode* node = timelines_.next(); !is_head(node) && node; node = node->next()) {
            static_cast<AnimTrack*>(node->data())->invoke_method_by_name(name, params, script);
        }
        return 1;
    case kOBJEnableAnimBlending: {                                                       // flag4C from enabled
        int enabled = 1; // (missing: on - the original read an argument's stack slot, an address: never 0)
        params->fetch_int_value(kEnabled, &enabled, 1);
        flag_4c_ = enabled != 0;
        return 1;
    }
    case kAnimEquals: // AnimEquals
    case kObjAnimEquals:
        return is_anim_one_of(params, script) ? 1 : 0;
    case kPassAnimToTimelineMethod: // anim (Default when there's none) to timeline Index's (the playing one's) controllers
        return pass_anim_to_timeline(params, script);
    case kSetWobbleDetails:
        return 1;
    case kTimelineForwardMethodA:
    case kTimelineForwardMethodB:
    case kAddAnimController:
    case kSetPartialAnimSpeed: // SetPartialAnimSpeed
    case kGetPartialAnimParams: // GetPartialAnimParams
    case kTimelineForwardMethodC: {
        const memory::ListNode* node = timelines_.next();
        if (is_head(node) || !node) {
            return 1;
        }
        return static_cast<AnimTrack*>(node->data())->invoke_method_by_name(name, params, script) != 0 ? 1 : 0;
    }
    default:
        return 2;
    }
}

// The info of timeline Index (the playing one without it) to the script.
int BlendChannel::add_timeline_info_from_script(Struct* params, script::Script* script)
{
    int index = 0;
    if (!params->fetch_int_value(kIndex, &index, 0)) {
        const memory::ListNode* node = timelines_.next();
        if (!is_head(node) && node) {
            add_timeline_info(static_cast<AnimTimeline*>(node->data()), script->args().get());
        }
        return 1;
    }
    for (AnimTrack* timeline = masked(timelines_.next()); timeline; timeline = masked(elem(timeline)->next())) {
        if (index == 0) {
            add_timeline_info(timeline, script->args().get());
            break;
        }
        --index;
    }
    return 1;
}

// Flip: flipped, and every timeline's (or timeline Index's) controllers told.
int BlendChannel::flip_from_script(uint32_t name, Struct* params, script::Script* script)
{
    int index = 0;
    if (!params->fetch_int_value(kIndex, &index, 0)) {
        toggle_flipped();
        for (const memory::ListNode* node = timelines_.next(); !is_head(node) && node; node = node->next()) {
            static_cast<AnimTrack*>(node->data())->invoke_method_by_name(name, params, script);
        }
        return 1;
    }
    if (index == 0) {
        toggle_flipped();
    }
    for (AnimTrack* timeline = masked(timelines_.next()); timeline; timeline = masked(elem(timeline)->next())) {
        if (index == 0) {
            timeline->invoke_method_by_name(name, params, script);
            break;
        }
        --index;
    }
    return 1;
}

// The wait_anim member function (0x0047FDCE): the script waits until the playing timeline reaches a time - in
// frames (Frame / Frames), a percentage of its range (Percent), seconds (0x49E0EE96 / Seconds) or milliseconds -
// from the current time (0x91A4C826), the end (FromEnd) or its start; the object's script made if it has none.
int BlendChannel::wait_anim(Struct* params)
{
    float start = 0.0F;
    float current = 0.0F;
    float end = 0.0F;
    objects::get_channel_times(this, &start, &current, &end);
    float time = 0.0F;
    params->fetch_float(0U, &time, 0);
    if (params->has_bare_flag(kFRAME) || params->has_bare_flag(kFrames)) { // Frame, Frames
        time = time * kSecondsPerFrame;
    } else if (params->has_bare_flag(kPercent)) { // Percent
        time = static_cast<float>(
            ((static_cast<double>(end) - start) * time * static_cast<double>(kPercentToFraction)) + start);
    } else if (!params->has_bare_flag(kSecond) && !params->has_bare_flag(kSeconds)) { // (Seconds: as it is)
        time = time * kMillisecondsToSeconds;
    }
    const bool forwards = start <= end;
    // (x87: rounded once to a float; a NaN kept as it is - not negated)
    auto sum = [](float a, float b) { return static_cast<float>(static_cast<double>(a) + b); };
    auto difference = [](float a, float b) { return static_cast<float>(static_cast<double>(a) - b); };
    if (params->has_bare_flag(kRelative)) {
        wait_time_ = forwards ? sum(time, current) : difference(current, time);
    } else if (params->has_bare_flag(kFromEnd)) { // FromEnd
        wait_time_ = forwards ? difference(end, time) : sum(time, start);
    } else {
        wait_time_ = time;
    }
    if (forwards ? wait_time_ > end : wait_time_ < end) {
        wait_time_ = end;
    }
    objects::GameEntity* owner = get_owner(this);
    if (!owner->own_script()) {
        owner->set_own_script(script::Script::create());
    }
    if ((start <= end && current >= wait_time_) || (start >= end && current <= wait_time_)) {
        owner->own_script()->stop_waiting(); // (already there)
        waiting_ = false;
    } else {
        owner->own_script()->gate_off(0);
        waiting_ = true;
    }
    return 1;
}

// The member function 0x922E7D14 (0x004800D3): whether the time given (the unnamed float, in frames, from the
// start) lies between the playing timeline's time last frame and now - either way round.
int BlendChannel::time_passed(Struct* params)
{
    float start = 0.0F;
    float current = 0.0F;
    float end = 0.0F;
    objects::get_channel_times(this, &start, &current, &end);
    float time = 0.0F;
    params->fetch_float(0U, &time, 0);
    time = time * kSecondsPerFrame;
    auto now = static_cast<float>(static_cast<double>(current) - start);
    auto before = static_cast<float>(static_cast<double>(prev_timeline_pos_) - start);
    if (now == time) {
        return 1;
    }
    if (before < time && time < now) {
        return 1;
    }
    if (now < before && before < time) {
        return 1;
    }
    if (time < now && now < before) {
        return 1;
    }
    return 0;
}

// The member function 0xC68365EB (0x00480212): anim (Default added when the anim set hasn't it) and its length
// (Duration) to timeline Index's controllers (the playing one's without it - its tags then set from the anim). Their
// answer (as true / false), or false without such a timeline.
int BlendChannel::pass_anim_to_timeline(Struct* params, script::Script* script)
{
    uint32_t anim = 0; // (missing: 0 - the original read an argument's stack slot, an address no name has)
    params->read_hash_value(kAnim, &anim, 0);
    if (!has_value(anim)) {
        anim = kDefault;
        params->put_name_key(checksums::Name{kAnim}, kDefault);
    }
    params->push_float(checksums::Name{kDuration}, fetch_value(anim)); // Duration
    int index = 0;
    AnimTrack* timeline = masked(timelines_.next());
    if (params->fetch_int_value(kIndex, &index, 0)) {
        for (; timeline && (index != 0); --index) {
            timeline = masked(elem(timeline)->next());
        }
    }
    if (!timeline) {
        return 0;
    }
    if (timeline == masked(timelines_.next())) {
        clear_flag();
        set_anim_tags(anim);
    }
    return timeline->invoke_method_by_name(kPassAnimToTimelineMethod, params, script) != 0 ? 1 : 0;
}

} // namespace gfx

namespace gfx {

namespace {

// 64 quaternions nothing reads (only their static initialiser writes them).
std::array<float, kSpareRotationFloats>& spare_rotations()
{
    static std::array<float, kSpareRotationFloats> spare_rotations{};
    return spare_rotations;
}

} // namespace

// 0x00640AA0 (a static initialiser): each (0, 0, 0, 1).
void construct_spare_rotations()
{
    for (int i = 0; i < kSpareRotationCount; ++i) {
        core::at(spare_rotations(), (4 * i) + 0) = 0.0F;
        core::at(spare_rotations(), (4 * i) + 1) = 0.0F;
        core::at(spare_rotations(), (4 * i) + 2) = 0.0F;
        core::at(spare_rotations(), (4 * i) + 3) = 1.0F;
    }
}

} // namespace gfx
