#pragma once

// goals::ObjectiveNpc (0x14 bytes; 0x0055EB40-0x005600CF): a goal's pro - the ped who gives the goal: its speech
// (streams found by name: the pro's first name, the goal's name, the difficulty level, a camera animation's
// number, a suffix, _f for a female skater, _success; a lip-synced version first), its facial animations (a .fam
// loaded with the stream), its trigger object (hidden, killed), its arrow.

#include <cstdint>
#include <span>

namespace script {
class Struct;
} // namespace script

namespace goals {

class ObjectiveNpc {
private:
    uint32_t objective_key_{}; // +0x00
    bool suspended_;         // +0x04: its logic suspended (no streams)
    uint32_t fam_;           // +0x08: the loaded .fam's checksum (0: none)
    uint32_t anim_object_;   // +0x0C: the object playing the facial animation (its id, or skater; 0: none)
    uint32_t feed_;          // +0x10: the stream playing (0: none)

public:

    // 0x0055EB40: the constructor: `params`' goal_id (asserted), nothing playing.
    explicit ObjectiveNpc(script::Struct* params);

    // 0x0055EB70 (cdecl): the first word of `*name` (up to its first space, `*name` left there) into `out` - the
    // whole name if there's no space. (`size` isn't read.)
    static void read_leading_name(std::span<char> out, const char* name);
    // 0x0055EBC0 (cdecl): with `add`, _success appended to `text` (sprintf onto itself, as the original). (`size`
    // isn't read.)
    static void add_success_suffix(bool add, char* text, int size);
    // 0x0055EBF0: the goal's pro's first name (`pro`) - for the skater being that pro, its alternative (0x878CCC12, or
    // null); null if the goal has no pro.
    const char* get_pro_given_name();
    // 0x0055ED00: the stream to play: the first that exists (a _lip version first) of <pro>_<goal>[_<difficulty>]
    // [_<suffix>][_f][_success], or (with a camera animation `index` and not `no_cam_anim`) the _camAnim<index>
    // versions. Its checksum into `out` (0: none); whether it's a lip-synced one. False (nothing written) without
    // a pro or for an unknown difficulty level.
    bool get_voice_clip_key(uint32_t* out, int index, bool success, const char* suffix, bool no_cam_anim);
    // 0x0055F170: the player's first name into `out`. (`size` isn't read.)
    void get_skater_given_name(std::span<char> out);
    // 0x0055F1B0: the last stream stopped (and its object's goal_play_stream scripts).
    void cut_previous_voice_line();
    // 0x0055F210: the goal's current stream (0xDC14D454) dropped and stopped.
    void cut_voice_line();
    // 0x0055F270: the animations array for `type`: <goal>_<type>, <pro>_goal_<type> or generic_pro_anims_<type> (the
    // last whether it exists or not) - its checksum; 0 without `type` when the first isn't there.
    uint32_t get_objective_anim_sets(const char* type);
    // 0x0055F3D0: the trigger object (trigger_obj_id) killed (its vtable +0x18).
    void remove_objective_npc();
    // 0x0055F420: suspended (goal_pro_stop_anim_scripts, the current stream stopped) or resumed.
    void halt_objective_updates(bool suspend);
    // 0x0055F460: the facial animation removed (the object's jawRotation PartialAnim) and the .fam unloaded.
    void unload_anim_bank();
    // 0x0055F5B0: the trigger object hidden (its arrow killed) or shown (its arrow added unless the goal is won);
    // not for null goals.
    void conceal(bool hide);
    // 0x0055F710: the stream's .fam (fam\<stream>.fam) loaded into `anim_set`.
    void load_anim_bank(uint32_t stream, uint32_t anim_set);
    // 0x0055F7A0: `stream` played (goal_play_stream) by the object `objectId` (0: the trigger object; skater: the
    // skater) - with its animation (and with `load_fam` its .fam) unless `no_anim`. Not while suspended or for a
    // stream that doesn't exist.
    void play_stream(uint32_t stream, bool no_anim, uint32_t object_id, bool load_fam);
    // 0x0055F960 / 0x0055FA60: the goal's start (cam_anim_index, speaker_name, last_anim, speaker_obj_id) / win
    // (_success) stream - not while suspended (nor with no_stream).
    void play_opening_voice_line(script::Struct* params);
    void play_victory_voice_line(script::Struct* params);
    // 0x0055FB00: one of the pro's `count` (+20 with `call_by_name`) streams of `kind` in a random order:
    // <pro>_<kind><n> or, past `count`, <pro>_<kind><player><n - count> (the player being a pro).
    void play_pro_voice_line(const char* kind, int count, bool call_by_name, bool no_anim);
    // 0x0055FDF0: a Far stream.
    void play_idle_voice_line();
    // 0x0055FE10: a stream of `type` (VO: that one; play_random / call_player_by_name: the pro calling the player by
    // name, else midgoal / wait / `type`'s pro streams).
    void play_objective_voice_line(const char* type, script::Struct* params);
};

} // namespace goals
