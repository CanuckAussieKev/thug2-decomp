#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/sound/sfx.h"

namespace script {
class Script;
class Struct;
} // namespace Script

namespace objects {

struct EmitterNode;
struct ProximNode;

// The sound component (type 0x07713C7B, "sound"; vtable 0x00646514, 0xC0 bytes; made by 0x00433100): the sounds its
// object makes, through the sound effects manager (sound::AudioMixer 0x006F85A8 - which takes the component as its
// "object"): played at the object (or at the nearest point of its emitter to the camera, heard from the nearest point
// of its proxim node), adjusted, stopped; a looping sound too far away to hear is kept (two at most) and started once
// the camera comes near enough. Its script member functions: Obj_PlaySound (the table at 0x00683118, filled by
// 0x006423F0), and Obj_AdjustSound / Obj_StopSound / Obj_SetSound by name.
//
// Vtable: 0 the scalar deleting destructor; 1 Update; 2 configure_from_data; 3 / 4 the empty 0x005480A0; 5 the shared
// 0x004E3F90; 6 0x0048DCD0; 7 the base's Suspend; 8 / 10 the empty 0x005BAB90; 9 0x004A1090 (shared); 11 0x0042BB20
// (shared); 12 invoke_method_by_name; 13 0x0048D090 (empty, RET 0xC: told of each sound before it's played).
class AudioEmitterModule : public ObjectModule {
public:
    static constexpr uint32_t kType = 0x07713C7B;

    // A looping sound waiting to be heard: its play parameters and when to look again (frame_clock::GetTime).
    struct WaitingSound {
        sound::PlaybackProps props; // +0x00 (props.checksum 0: none)
        uint32_t time;            // +0x2C
    };

    // 0x00433100 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static ObjectModule* __cdecl fabricate();
    // 0x00432C80 (not a Ghidra function; registered with Create): its script member functions registered.
    static void __cdecl publish_method_table();
    // 0x00432D90: the base's constructor, its vtable and type; no emitter or proxim node; at the origin (w 1) now and
    // before. (The sound slots and waiting sounds are left as allocated - zero.) Returns this.
    AudioEmitterModule* make_new();
    // 0x00432DF0: every sound of it stopped (the manager's stop_object_sounds); the base's body.
    void destroy_body();
    // 0x00433160 (vtable slot 0)
    AudioEmitterModule* teardown(unsigned int flags);
    // 0x00432CA0 (vtable slot 2): ProximNode= (the proxim node heard from).
    void configure_from_data(script::Struct* params);
    // 0x004335D0 (vtable slot 1): the position a frame ago kept, the object's taken; then update_waiting_sounds.
    void tick_frame();
    // 0x00433190 (vtable slot 12): Obj_AdjustSound, Obj_StopSound (the unnamed sound stopped and no longer waited
    // for), Obj_SetSound: 1; other names 2.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script);

    // 0x00432D40: a looping sound kept to start later (in the first free place; none: dropped), looked at again 2
    // seconds after `time`.
    void keep_looping_sound(const sound::PlaybackProps* props, uint32_t time);
    // 0x00432E50 / 0x00432EB0: where the sound is heard from by `camera` now / a frame ago: the nearest point of the
    // emitter (if any, and given a camera), else the position.
    maths::Vec4* get_sound_position(maths::Vec4* out, void* camera);
    maths::Vec4* get_previous_sound_position(maths::Vec4* out, void* camera);
    // 0x00432F10: the nearest point of the proxim node to `camera` (false without one or without a camera).
    bool get_sound_extent(void* camera, maths::Vec4* out);
    // 0x00432F70 (`this` unused): the props' pitch and volume moved towards their targets (+0x18 / +0x1C) by their
    // steps (+0x20 / +0x24) a 60th of a second.
    void update_props(sound::PlaybackProps* props);
    // 0x00433480: each waiting sound eased (update_props) and, once its time comes, played if the nearest camera is
    // within its dropoff (1020 when 0) + 240 - forced, then faded in to its volume - else looked at again later
    // (sooner the nearer it is).
    void update_waiting_sounds();
    // 0x00433240: a sound played (Obj_PlaySound): the unnamed checksum, or the slot `Type` (0-8) set by Obj_SetSound;
    // vol / pitch (100), or Percent between floor_loudness_..maxVol / minPitch..maxPitch; dropoff (feet), dropoff_function,
    // NoPosUpdate, emitter.
    void emit_sound(script::Struct* params);
    // 0x00432FF0 Obj_AdjustSound: the unnamed sound's (playing, or waiting) target volume / pitch = loudness_percent /
    // pitchPercent of their originals, reached at once unless volumeStep / pitchStep are given.
    void adjust_sound(script::Struct* params, script::Script* script);
    // 0x00432CE0 Obj_SetSound: slot type= (0-8) = the unnamed checksum.
    void assign_sound_cue(script::Struct* params);

    // --- script member functions (the table at 0x00683118) ---
    // 0x00433630 (not a Ghidra function) Obj_PlaySound: emit_sound. True.
    bool obj_play_sound(script::Struct* params, script::Script* script);

    uint32_t sound_cues_[8];          // +0x18 the sounds by type (Obj_SetSound - which takes 8 too: the next field)
    WaitingSound waiting_[2];     // +0x38
    maths::Vec4 at_point_;             // +0x98 where it is
    maths::Vec4 last_pos_;         // +0xA8 where it was a frame ago
    EmitterNode* spewer_;       // +0xB8 heard from its nearest point
    ProximNode* proxim_;         // +0xBC
};
static_assert(sizeof(AudioEmitterModule::WaitingSound) == 0x30, "layout must match the original");
static_assert(offsetof(AudioEmitterModule, sound_cues_) == 0x18 && offsetof(AudioEmitterModule, waiting_) == 0x38 &&
                  offsetof(AudioEmitterModule, at_point_) == 0x98 && offsetof(AudioEmitterModule, last_pos_) == 0xA8 &&
                  offsetof(AudioEmitterModule, spewer_) == 0xB8 && offsetof(AudioEmitterModule, proxim_) == 0xBC &&
                  sizeof(AudioEmitterModule) == 0xC0,
              "layout must match the original");

} // namespace Obj
