#pragma once

// Blending animation poses (0x0047D420-0x0047DC31): the poses the controllers of an object's animation blender
// fill in are mixed by their channels' weights, lower channels first taking in the higher ones; a few small pieces
// of the blender channel and the timeline sit here too.

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"

namespace objects {
class AnimBlenderComponent;
class ObjectModule;
class GameEntity;
} // namespace objects
namespace script {
class Script;
class Struct;
} // namespace script

namespace gfx {

struct AnimTimeline;

// A pose (0x840 bytes): a rotation (quaternion) and a translation per bone, and whether each bone has one.
struct Stance {
    float rotations[64][4];    // +0x000
    float translations[64][4]; // +0x400
    uint8_t valid[64];         // +0x800
};
static_assert(sizeof(Stance) == 0x840 && offsetof(Stance, valid) == 0x800, "layout must match the original");

// 0x0047D470: the rotation a fraction `t` of the way from `from` to `to` (the shorter way: `to` is negated first
// when the two point apart), normalised. As the original, `to` is overwritten with the result too.
float* interpolate_rotation(float* out, const float* from, float* to, float t);

// 0x0047D680: `count` poses mixed into the first by `weights` (weights[0] becomes 1 minus the others), `bones`
// bones each; the result copied to `out` if that isn't `poses`. The poses' rotations change on the way.
void mix_poses(Stance* poses, float* weights, int bones, Stance* out, int count);
// 0x0047D8F0: the same for the poses whose `ids` are `id` only (weights below 0.00001 left out); a bone a pose
// takes from a higher one is marked valid.
void blend_poses_by_id(Stance* poses, float* weights, const uint32_t* ids, int bones, Stance* out, int count,
                       uint32_t id);

// 0x0047D580 (code nothing calls): the timeline's animation, speed, loop type and times added to `info`.
void add_timeline_info(AnimTimeline* timeline, script::Struct* info);

class AnimTrack;

// An animation blender's channel (0x74 bytes; the blender keeps them on a list, so it's a list node itself): its
// timelines (AnimTrack, the newest - the one playing - first; older ones blend out) and a blend value moving to a
// target. (The original's one table, 0x00648714 - 0 Destroy, 1 tick_frame(pose index, blend), 2 Finalize, 3
// configure_from_data (0x00480410), 4 invoke_method_by_name (0x0047FA00), 5 (0x005BAB90), 6 release_waiting_script -
// had no other class: plain methods here, called by name.)
class BlendChannel {
public:
    // 0x0047D420
    void set_flag_4c(bool value);
    // 0x0047D430: blending to `target` over `period` (at once when it's 0: the current value is the target too);
    // the blend starts from the current value.
    void blend_to(uint32_t target, float period);
    // 0x0047D620 / 0x0047D640: the value `offset` past the channel's key (0x0048CF50 finds it), and whether there
    // is one.
    float fetch_value(uint32_t offset);
    bool has_value(uint32_t offset);
    // 0x0047D660 (code nothing calls): flag 0x5DB4115F taken out of the owner's parameters.
    void clear_flag();

    // --- 0x0047DC40-0x0047F24E ---
    // 0x0047F150: on `object` / `blender`, no timelines, events on, the blend value 1 (with `full`) or 0; the
    // rest as Reset (it reads +0x60 before setting it, as the original).
    BlendChannel(objects::GameEntity* object, objects::AnimBlenderComponent* blender, bool full);
    // 0x0047EE90: its timelines deleted, the head and its own node unlinked.
    // 0x0047F230 (slot 0): that, then freed.
    void destroy_body();
    BlendChannel* teardown(uint8_t flags);
    // 0x0047EF20: its timelines deleted, no next blend period, no time waited for (the script stops waiting), no
    // first time.
    void get_reset();
    // 0x0047DCF0 (slot 2): the object's skeleton component kept.
    void seal();
    // 0x0047EFB0 (slot 1): the blend value moved on, each timeline updated, the done ones deleted; with `blend` and
    // the blender's model ready, its pose blended into the blend buffer's `index` (blend_pose). Whether it was.
    bool tick_frame(int index, bool blend);
    // 0x0047EF60 (slot 6): a script waiting on the animation (wait type 6) let go once it's complete.
    void release_waiting_script(script::Script* script);
    // 0x0047E030: the playing timeline's time kept (+0x5C), the blend value a frame on, and the time waited for
    // checked (the object's own script made if it has none).
    void update_blend();
    // 0x0047E1B0: each timeline's Update (slot 2).
    void update_timelines();
    // 0x0047DDC0: the timelines no longer active unlinked and deleted.
    void remove_done_timelines();
    // 0x0047DE20: the timelines past the most there may be (0x006F8A68) deleted, the playing one blended out over
    // `period` with the weight the others leave it, and a new timeline (0x00481EE0, AnimTrack::make_new) put
    // first.
    void push_timeline(float period);
    // 0x0047ED60: each timeline's pose (fetch_pose) from the skeleton's base translations, blended (mix_poses) and
    // copied to the blend buffer's `index`; false with rendering skipped (0x007D1EC0), no anim set or no skeleton.
    bool blend_pose(int index);
    // 0x0047EFF0: blend_pose(0).
    bool blend_first_pose();
    // 0x0047EAA0: timeline `index`'s weight into `weight` and (when above 0 or NaN) its pose into `pose` (a missing
    // timeline crashes, as the original).
    void fetch_pose(int index, void* pose, float* weight);

    // 0x0047DC40: the flip flag (bit 29 of +0x54) set to `flip` - with `send` (channel 0 of this machine's player's
    // skater) the server told (message 0x2C); the flag it had. `unused` isn't.
    bool set_flipped(uint32_t object_id, bool flip, uint32_t unused, bool send);
    // 0x0047DD80: flipped the other way, the server told.
    void toggle_flipped();
    // 0x0047E010: `frames` added to the playing timeline's time (AnimTimeline::advance_clock).
    void advance_clock(float frames);
    // 0x0047E210 / 0x0047E240 (not a Ghidra function): whether the playing animation is complete / cycles or
    // ping-pongs (false without one).
    bool is_clip_done();
    bool is_cycling();
    // 0x0047DF50 (not a Ghidra function): whether the playing animation (+0x30) is one of the unnamed checksums - or
    // in the unnamed arrays, or the arrays a checksum names - of `params`. `script` isn't used.
    bool is_anim_one_of(script::Struct* params, script::Script* script);
    // 0x0047E280: the wobble target (0..1 when sent: message 0x2B with it x 255, only when it changed) to the playing
    // timeline's controllers (choose_wobble_goal 0xD0209498 with 0x4D747FA0).
    void choose_wobble_goal(float target, bool send);
    // 0x0047E450: the five wobble details (message 0x3C: which of two known values each is, when it changed) to the
    // playing timeline's controllers (SetWobbleDetails 0xEA6D0EFD).
    void set_wobble_details(const float* details, bool send);
    // 0x0047E740: the playing timeline's loop type (message 0x3D when it changed).
    void change_repeat_mode(int loop_type, bool send);
    // 0x0047E810: the playing timeline turned round. (`unused` isn't.)
    void get_reverse(uint32_t unused);
    // 0x0047E850: the playing timeline's speed (message 0x41; unless `all`, nothing when within 0.05 of it); with
    // `all` every timeline's - with `keep_ratio` each one's range times the first's speed per frame of range.
    void set_speed(float speed, bool send, bool all, bool keep_ratio);
    // 0x0047EA40 (not a Ghidra function): the playing timeline's speed per second (unrounded, ST0), 0 without one.
    double get_speed(script::Struct* params, script::Script* script);
    // 0x0047EA80: events on or off (here and on the playing timeline; nothing without one).
    void set_events_on(bool on);
    // 0x0047F000: the object's tags (set_tags_from_struct) set to AnimTags 0x5DB4115F = the structure `anim` names in
    // the global structure 0x95807202.
    void set_anim_tags(uint32_t anim);

    // 0x00480410 (slot 3): SubsetId, the blend value (0x4D786F62), priority (0x3D836C96), AnimEventTableName, the
    // anim set (0x52A864B0, else 0x09794932 asserted); Default played (0 to 1000), reset, then 0xEAE64B47 (Default)
    // played over its length (looping when it was given), flag4C (0xCF466AA2), events (0xE1B3F630).
    void configure_from_data(script::Struct* params);
    // 0x0047F260: `anim` (Default when 0) played from `from` to `to`: with `send` the server told (message 0x29: loop
    // type, object, anim, the times, blend period and speed x 4096); the older timelines deleted when there's no
    // blend period or no flag4C; a new timeline pushed and given the play (its slot 6, with the flip flag and
    // `params`); the object's AnimTags taken out and set again from `anim` (set_anim_tags).
    void play_anim(uint32_t anim, bool send, float from, float to, int loop_type, float blend_period, float speed,
                   script::Struct* params);
    // 0x004803D0: `anim` played once over its length (0x0048CF50's first value) at speed 1, not sent.
    void play_animation(uint32_t anim, float blend_period);
    // 0x0047F470 (not a Ghidra function): PlayAnim / Obj_PlayAnim's parameters played (sent) - the anim (Default when
    // it isn't one; Current: the playing one; nothing with NoRestart when it plays), speed, the blend period (the
    // one set for the next play, a percentage 0x4F3ABD6C of its length or BlendPeriod, else `blendPeriod`), the
    // start synchronised with the playing animation (0x099371AE / 0xFF5AC79E: its fraction - Backwards: from the end
    // -, at its speed), the range and loop type (0x004877E0 / 0x00487770), 0x84F13067 (flag4E: wait for the playing
    // one to complete), Duration 0x23272E4D, and a partial animation 0x6C565F57 added as a controller. The anim
    // played, or 0 (none: no anim set, or still waiting with flag4E).
    uint32_t play_anim_from_script(script::Struct* params, script::Script* script, float blend_period);
    // 0x0047FA00 (slot 4): the channel's script member functions (the timelines' too: anything else passed on to the
    // playing timeline); 0 false, 1 true / handled, 2 not its own.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script);
    // Pieces of invoke_method_by_name: wait_anim (0x74CF5C94), 0x922E7D14 (whether a time was passed this frame) and
    // 0xC68365EB (anim and its length to a timeline's controllers).
    int wait_anim(script::Struct* params);
    int time_passed(script::Struct* params);
    int pass_anim_to_timeline(script::Struct* params, script::Script* script);

    // 0x0047EB30 (cdecl): the blend buffers on the AnimScratch heap (0x7C0B34B4): `subsets` poses (0x006F8A60), and
    // `blends` poses (0x006F8A64), sets (0x006F8A58) and weights (0x006F8A5C) - the poses' rotations the identity.
    static void init_buffers(int blends, int subsets);

    memory::ListNode list_link_{}; // +0x00 (data = this; the original's node size, as the head's)
    memory::ListNode timelines_{}; // +0x14 the head (data -1)
    uint32_t ident_;             // +0x28
    int rank_;                   // +0x2C (0x3D836C96)
    uint32_t ongoing_;           // +0x30 the blend value (a float)
    uint32_t aim_point_;         // +0x34
    uint32_t outset_;            // +0x38
    float period_;               // +0x3C
    float period_left_;          // +0x40
    bool has_next_blend_{};      // +0x44 (the next play blends over next_blend_)
    uint8_t pad45_[3]{};
    float next_blend_{};                     // +0x48
    bool flag_4c_;                           // +0x4C (without it each play deletes the older timelines)
    bool events_on_;                         // +0x4D
    bool flag_4e_{};                         // +0x4E
    uint32_t lookup_key_;                    // +0x50 the anim set
    uint32_t flag_bits_{};                   // +0x54 (bit 29: flipped)
    uint32_t event_table_;                   // +0x58 (AnimEventTableName 0x5B8C6DC2)
    float prev_timeline_pos_{};              // +0x5C the playing timeline's time last frame
    bool waiting_{};                         // +0x60 a time waited for
    float wait_time_{};                      // +0x64
    uint8_t* owner_ptr_;                     // +0x68 the object (its script at +0x10, id at +0x14, parameters at +0x20)
    objects::AnimBlenderComponent* blender_; // +0x6C
    objects::ObjectModule* bone_rig_{};      // +0x70 the object's skeleton component
};

// (The channel's timeline list's node type - AnimTimeline::destroy_body its body, 0x0047DD40 its scalar deleting
// destructor - is memory::ListNode's.)

// A list's head (lists::Foremost; the channel's timelines, a server list): 0x0047DD10, how many items it has.
struct AnimListHead {
    int tally_entries();
    memory::ListNode head_;
};

// The blend's shared buffers: the weights, the bone set of each channel, the pose.
extern Stance* g_blend_buffer;
extern float* g_blend_weights;
extern uint32_t* g_blend_sets;

} // namespace gfx
