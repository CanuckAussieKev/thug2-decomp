#pragma once

// An animation track: one animation playing on an object's blend channel (the channel keeps a list of them - the
// newest plays, the ones it replaced fade out). A timeline (gfx::AnimTimeline) with the controllers that fill in
// the pose from it (up to six, by priority), the animation's events (from the anim set's event table) and a
// fading weight.
//
// (The original's table 0x00648794: 0 Destroy (0x00482340), 1 Reset (0x00481960), 2 Update (0x00481AC0), 3 the
// timeline's 0x00481430, 4 0x005BAB90 (nothing), 5 invoke_method_by_name (0x00482200), 6 PlayAnim (0x004823C0) -
// called only on tracks, by the blend channel: plain methods.)
// (Its members 0x00481830-0x004819D0 are in anim_timeline.cpp.)

#include <cstddef>
#include <cstdint>
#include <memory>
#include <array>
#include <vector>

#include "engine/gfx/anim_controller.h"
#include "engine/gfx/custom_anim_key.h"

namespace memory {
struct PoolPair;
}
namespace objects {
class GameEntity;
}
namespace script {
class Script;
class Struct;
} // namespace script

namespace gfx {

constexpr std::size_t kControllerCount = 6; // the drivers a track blends, by priority

// An animation event: gfx::AnimScriptKey (custom_anim_key.h) - at a frame of the animation, an event type with
// parameters, sent to the object once the timeline passes it while armed.
using TimedClipEvent = AnimScriptKey;

class AnimTrack : public AnimTimeline {
public:
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): held in the original's integer fields (to convert with them)
    enum State : uint8_t { kOff = 0, kFading = 1, kPlaying = 2 };

    // 0x00481EE0 / 0x00482360: the class's operator new / delete (its item pools 0x006F92C8).
    static void* operator new(size_t size);
    static void operator delete(void* item);

    // 0x00481830: the timeline's constructor, then no controllers and no events, `object`, events on, playing.
    explicit AnimTrack(objects::GameEntity* object);
    // 0x00481960 (slot 1): the timeline's Reset, then off with no fade.
    void reset_track();
    // 0x00481870: each controller deleted (its slot 0 with 1; null ones skipped), none left.
    void delete_controllers();
    // 0x004818B0: `controller` (if it's one of them) deleted and the rest moved down.
    void detach_anim_driver(AnimDriver* controller);
    // 0x00481920: each controller's pose (slot 1) into `pose` (no null check). True.
    bool get_poses(Stance* pose);
    // 0x00481980 / 0x00481990: whether it isn't off / is fading.
    bool is_operative();
    bool is_fading();
    // 0x004819A0: each event armed again (0x00486150 with true).
    void arm_events();
    // 0x004819D0
    objects::GameEntity* fetch_subject();

    // 0x00481F20: its controllers and events deleted, then the timeline's destructor (0x00481690).
    ~AnimTrack();
    AnimTrack(const AnimTrack&) = delete;
    AnimTrack& operator=(const AnimTrack&) = delete;
    // 0x00482340 (slot 0)
    AnimTrack* teardown_track(uint8_t flags);
    // 0x00481AC0 (slot 2): not complete; fading, the weight drops by the frame's share of the fade rate (off at
    // nothing left, or once the fade times its scale goes below 0); playing or fading, each controller's Update.
    void tick_track();
    // 0x00482200 (slot 5): AddAnimController (1), RemoveAnimController (the one with `id`), 0xC68365EB (a new
    // animation and duration - Sync keeps the timeline's place in proportion); anything else to the controllers in
    // turn until one doesn't answer 2 (not its own).
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script);
    // How an animation plays: its start and end times, its loop type, the blend period and the speed.
    struct AnimPlay {
        float start{};
        float end{};
        int loop_type_{};
        float blend_period_{};
        float speed{};
    };

    // 0x004823C0 (slot 6): the animation played - its events, the standard (or with RepeatMode 3 the wobble)
    // controller, a weighted one (`params` with 0xD697A799 or flag 0x1B92121C), one for the structure 0xAEEEC12E,
    // the FlipRotate one; then each controller told 0xD0209498 (fraction 0) and PlayAnim with all of it.
    void play_anim(uint32_t anim, AnimPlay anim_play, bool flipped,
                   script::Struct* params);

    // 0x00481B40: starts fading out over `period` if playing (true); a period of 0 stops it at once.
    bool start_fade(float period);
    // 0x00481B80: its weight - 1 playing, the fade times its scale fading, 0 off (ST0, unrounded).
    double get_weight();
    // 0x00481FB0: a controller made from `params` (its "type"; null for an unknown one) and added by its
    // "priority" (500, FlipRotate 100).
    AnimDriver* attach_controller(script::Struct* params);
    // 0x00481A30: `controller` inserted before the first one of lower priority.
    void insert_controller(AnimDriver* controller, int priority);
    // 0x00481A80: the controller with `id`, or null.
    AnimDriver* find_controller(uint32_t id);
    // 0x00481C00 (not a Ghidra function): the events of `anim` from the object's anim set's event table (frame,
    // or time in seconds; event; params), armed.
    void load_events(uint32_t anim);
    // 0x00481BB0: the events deleted.
    void delete_events();
    // 0x00481E10 (not a Ghidra function): playing with events on, each event armed and fired at the object if it
    // lies between two times (in seconds, as frames); when the times run the wrong way for the direction (it
    // looped), the events are re-armed first and the first time becomes the timeline's start.
    void fire_events(float from, float to);

    static memory::PoolPair& get_pools();

    // The fields other code reads and sets.
    [[nodiscard]] float fade_left() const { return fade_left_; }
    [[nodiscard]] float fade_scale() const { return fade_scale_; }
    void set_fade_scale(float value) { fade_scale_ = value; }
    void set_events_on(bool value) { events_on_ = value; }

private:
    uint32_t unknown34_{};
    int cur_phase_{kPlaying};           // +0x38 State
    std::array<AnimDriver*, kControllerCount> controller_list_{};  // +0x3C (by priority, highest first)
    int controller_count_{0};           // +0x54
    float fade_rate_{};                 // +0x58 (1/60 when played)
    float fade_left_{};                 // +0x5C
    float fade_scale_{};                // +0x60
    objects::GameEntity* owner_object_; // +0x64
    std::vector<TimedClipEvent> events_; // the track's timed events (the original: an array at +0x68, its count at +0x6C)
    bool events_on_{true};                         // +0x70
    std::array<uint8_t, 3> pad71_{};
};

} // namespace gfx
