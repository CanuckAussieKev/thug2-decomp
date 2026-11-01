#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/gfx/anim_controller.h"
#include "engine/objects/movie_manager.h"

// Two kinds of movie the movie manager plays (0x005871B0-0x005880DF), each an animation asset played on a timeline
// of its own:
// - an object animation (vtable 0x0064EFE8, 0x64 bytes; asset kind 5): every object the asset names is moved and
//   turned to its frame each update;
// - a camera animation (vtable 0x0064F020, 0x94 bytes; the manager's cutscene type): the movie's camera follows
//   the asset, optionally aimed at an object ("target", with offsets).
// Their other slots are the base movie's (VideoEntry) or one-line overrides of the original's shared stubs.
namespace objects {

class ObjectAnimMovie final : public VideoEntry {
public:
    ObjectAnimMovie(const ObjectAnimMovie&) = delete;
    ObjectAnimMovie& operator=(const ObjectAnimMovie&) = delete;
    // 0x005871B0: no asset, an empty timeline.
    ObjectAnimMovie();
    // 0x00587210 (0x00587370 the deleting one): the timeline's, then the base's.
    ~ObjectAnimMovie() override;

    // 0x005875D0 (slot 1): ReadIn(params): the base's, then the asset named like the movie (none: false) played
    // (looping with 0x5EA0E211, backwards with 0xF8CFD515) - or, with 0xEE721991, no asset: a timer of `frames`
    // (0x019176C5) frames; then started. True.
    bool read_in(script::Struct* params) override;
    // 0x005873A0: the timeline set to play `anim` (its length first), forwards or backwards.
    void perform(void* anim, int loop_type, bool backwards);
    // 0x00587270 (slot 3): Start: the asset's start (0x00482F10), if any. True.
    bool kickoff() override;
    // 0x00587280 (slot 5): is_finished: +0x1A set: true; +0x18 set: false; otherwise whether the timeline is complete.
    // 0x005872A0 (slot 6): +0x18 set and the timeline complete.
    bool is_finished() override;
    bool is_held_and_complete() override;
    // Slots 7, 9, 10: no camera; skippable; (the shared stubs).
    bool has_camera() override { return false; }
    bool can_be_skipped() override { return true; }
    // 0x005876A0 (slot 2, through the jump 0x00587F60): Update: the timeline stepped, the asset told the step, and
    // every object it names (by id) given its frame's rotation (matrix and display matrix) and position, and its
    // model told (component 0x286A8D26, slot 1); a missing object is reported. With the script global 0x2F510F45
    // each is printed too.
    void tick_frame() override;

    void* anim_;                 // +0x2C the animation asset (null: just a timer)
    gfx::AnimTimeline timeline_; // +0x30
};

class CameraAnimMovie final : public VideoEntry {
public:
    CameraAnimMovie(const CameraAnimMovie&) = delete;
    CameraAnimMovie& operator=(const CameraAnimMovie&) = delete;
    // 0x005873F0: no asset, an empty timeline, no target, the script's field of view.
    CameraAnimMovie();
    // 0x005872C0 (0x00587450 the deleting one): the timeline's, then the base's.
    ~CameraAnimMovie() override;

    float field_of_view_{}; // +0x2C (0: the script global 0x99529205)
    bool has_goal_;       // +0x30
    uint8_t pad31[3]{};
    uint32_t aim_point_;         // +0x34 the target object's name (0xC588EEBC: a fixed point, goal_shift_)
    float goal_shift_[4]{};      // +0x38 (0x906642BF)
    float look_offset_[4]{};     // +0x48 (0xA00D3D6E)
    bool target_offset_set_{};   // +0x58 (any of x, y, z not 0)
    bool look_offset_set_{};     // +0x59
    void* anim_;                 // +0x5C
    gfx::AnimTimeline timeline_; // +0x60

    // 0x00587F70 (slot 1): ReadIn(params): the base's; the field of view (0x17E7321E, if there); the asset or a timer
    // as ObjectAnimMovie's; the target; started; the camera placed (0x00587A50) and told the asset's first frame.
    bool read_in(script::Struct* params) override;
    // 0x00587480: the target from params (0x531E4D28) and its two offsets.
    void load_target(script::Struct* params);
    // 0x00587540: ObjectAnimMovie::Perform's twin.
    void perform(void* anim, int loop_type, bool backwards);
    // 0x00587320 / 0x00587330 / 0x00587350 (slots 3, 5, 6): as ObjectAnimMovie's.
    bool kickoff() override;
    bool is_finished() override;
    bool is_held_and_complete() override;
    // Slots 7, 9, 10: its camera shown; skippable; kept after it ends (the shared stubs).
    bool has_camera() override { return true; }
    bool can_be_skipped() override { return true; }
    bool keeps_camera_after() override { return true; }
    // 0x00587590 (slot 4): 0xA8D5A188 the target from params; 0xD7241A6C the target cleared; anything else the
    // base's (0x004A0CF0).
    bool invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;
    // 0x00588090 (slot 2): Update: the timeline stepped, the camera updated, the asset told the step, the camera
    // placed (0x00587A50).
    void tick_frame() override;
    // 0x00587A50: the camera placed and aimed.
    void place_camera();
};

extern float g_float_zero;

extern float g_anim_movies_one;
extern float g_anim_movies_seconds_per_frame; // 0x006463B8

} // namespace objects
