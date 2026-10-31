#pragma once

#include <cstddef>
#include <cstdint>

#include "game/gfx/anim_controller.h"
#include "game/obj/movie_manager.h"

// Two kinds of movie the movie manager plays (0x005871B0-0x005880DF), each an animation asset played on a timeline
// of its own:
// - an object animation (vtable 0x0064EFE8, 0x64 bytes; asset kind 5): every object the asset names is moved and
//   turned to its frame each update;
// - a camera animation (vtable 0x0064F020, 0x94 bytes; the manager's cutscene type): the movie's camera follows
//   the asset, optionally aimed at an object ("target", with offsets).
// Their other vtable slots are the base movie's (0x00468xxx) or shared stubs.
namespace Obj {

struct CObjectAnimMovie {
    CMovie m_base;
    void* m_anim;                // +0x2C the animation asset (null: just a timer)
    Gfx::CAnimTimeline m_timeline; // +0x30

    // 0x005871B0: the constructor. 0x00587210: the destructor body. 0x00587370 (slot 0): the scalar deleting
    // destructor.
    CObjectAnimMovie* Construct();
    void DestroyBody();
    CObjectAnimMovie* Destroy(uint8_t flags);
    // 0x005875D0 (slot 1): Load(params): the base's, then the asset named like the movie (none: false) played
    // (looping with 0x5EA0E211, backwards with 0xF8CFD515) - or, with 0xEE721991, no asset: a timer of `frames`
    // (0x019176C5) frames; then started. True.
    bool Load(Script::CStruct* params);
    // 0x005873A0: the timeline set to play `anim` (its length first), forwards or backwards.
    void Play(void* anim, int loopType, bool backwards);
    // 0x00587270 (slot 3): Start: the asset's start (0x00482F10), if any. True.
    bool Start();
    // 0x00587280 (slot 5): IsDone: +0x1A set: true; +0x18 set: false; otherwise whether the timeline is complete.
    // 0x005872A0 (slot 6): +0x18 set and the timeline complete.
    bool IsDone();
    int IsHeldAndComplete();
    // 0x005876A0 (slot 2, through the jump 0x00587F60): Update: the timeline stepped, the asset told the step, and
    // every object it names (by id) given its frame's rotation (matrix and display matrix) and position, and its
    // model told (component 0x286A8D26, slot 1); a missing object is reported. With the script global 0x2F510F45
    // each is printed too.
    void Update();
};
static_assert(offsetof(CObjectAnimMovie, m_timeline) == 0x30 && sizeof(CObjectAnimMovie) == 0x64,
              "layout must match the original");

struct CCameraAnimMovie {
    CMovie m_base;
    float m_fov;                 // +0x2C (0: the script global 0x99529205)
    bool m_hasTarget;            // +0x30
    uint8_t pad31[3];
    uint32_t m_target;           // +0x34 the target object's name (0xC588EEBC: a fixed point, m_targetOffset)
    float m_targetOffset[4];     // +0x38 (0x906642BF)
    float m_lookOffset[4];       // +0x48 (0xA00D3D6E)
    bool m_targetOffsetSet;      // +0x58 (any of x, y, z not 0)
    bool m_lookOffsetSet;        // +0x59
    uint8_t pad5A[2];
    void* m_anim;                // +0x5C
    Gfx::CAnimTimeline m_timeline; // +0x60

    // 0x005873F0 / 0x005872C0 / 0x00587450: the constructor, the destructor body, the scalar deleting destructor.
    CCameraAnimMovie* Construct();
    void DestroyBody();
    CCameraAnimMovie* Destroy(uint8_t flags);
    // 0x00587F70 (slot 1): Load(params): the base's; the field of view (0x17E7321E, if there); the asset or a timer
    // as CObjectAnimMovie's; the target; started; the camera placed (0x00587A50) and told the asset's first frame.
    bool Load(Script::CStruct* params);
    // 0x00587480: the target from params (0x531E4D28) and its two offsets.
    void LoadTarget(Script::CStruct* params);
    // 0x00587540: CObjectAnimMovie::Play's twin.
    void Play(void* anim, int loopType, bool backwards);
    // 0x00587320 / 0x00587330 / 0x00587350 (slots 3, 5, 6): as CObjectAnimMovie's.
    bool Start();
    bool IsDone();
    int IsHeldAndComplete();
    // 0x00587590 (slot 4): 0xA8D5A188 the target from params; 0xD7241A6C the target cleared; anything else the
    // base's (0x004A0CF0).
    bool CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x00588090 (slot 2): Update: the timeline stepped, the camera updated, the asset told the step, the camera
    // placed (0x00587A50).
    void Update();
    // 0x00587A50: the camera placed and aimed.
    void PlaceCamera();
};
static_assert(offsetof(CCameraAnimMovie, m_target) == 0x34 && offsetof(CCameraAnimMovie, m_lookOffsetSet) == 0x59 &&
                  offsetof(CCameraAnimMovie, m_timeline) == 0x60 && sizeof(CCameraAnimMovie) == 0x94,
              "layout must match the original");

extern float g_floatZero;

extern float g_animMoviesOne;
} // namespace Obj
