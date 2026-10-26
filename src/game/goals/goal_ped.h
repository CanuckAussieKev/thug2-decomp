#pragma once

// Game::CGoalPed (0x14 bytes; 0x0055EB40-0x005600CF): a goal's pro - the ped who gives the goal: its speech
// (streams found by name: the pro's first name, the goal's name, the difficulty level, a camera animation's
// number, a suffix, _f for a female skater, _success; a lip-synced version first), its facial animations (a .fam
// loaded with the stream), its trigger object (hidden, killed), its arrow.

#include <cstdint>

namespace Script {
class CStruct;
} // namespace Script

namespace Game {

class CGoalPed {
public:
    uint32_t m_goalId;     // +0x00
    bool m_suspended;      // +0x04: its logic suspended (no streams)
    uint8_t m_pad05[3];
    uint32_t m_fam;        // +0x08: the loaded .fam's checksum (0: none)
    uint32_t m_animObject; // +0x0C: the object playing the facial animation (its id, or skater; 0: none)
    uint32_t m_stream;     // +0x10: the stream playing (0: none)

    // 0x0055EB40: the constructor: `params`' goal_id (asserted), nothing playing. Returns this.
    CGoalPed* Construct(Script::CStruct* params);

    // 0x0055EB70 (cdecl): the first word of `*name` (up to its first space, `*name` left there) into `out` - the
    // whole name if there's no space. (`size` isn't read.)
    static void ParseFirstName(char* out, const char** name, int size);
    // 0x0055EBC0 (cdecl): with `add`, _success appended to `text` (sprintf onto itself, as the original). (`size`
    // isn't read.)
    static void AddSuccessSuffix(bool add, char* text, int size);
    // 0x0055EBF0: the goal's pro's first name (`pro`) - for the skater being that pro, its alternative (0x878CCC12, or
    // null); null if the goal has no pro.
    const char* GetProFirstName();
    // 0x0055ED00: the stream to play: the first that exists (a _lip version first) of <pro>_<goal>[_<difficulty>]
    // [_<suffix>][_f][_success], or (with a camera animation `index` and not `noCamAnim`) the _camAnim<index>
    // versions. Its checksum into `out` (0: none); whether it's a lip-synced one. False (nothing written) without
    // a pro or for an unknown difficulty level.
    bool GetStreamChecksum(uint32_t* out, int index, bool success, const char* suffix, bool noCamAnim);
    // 0x0055F170: the player's first name into `out`. (`size` isn't read.)
    void GetPlayerFirstName(char* out, int size);
    // 0x0055F1B0: the last stream stopped (and its object's goal_play_stream scripts).
    void StopLastStream();
    // 0x0055F210: the goal's current stream (0xDC14D454) dropped and stopped.
    void StopCurrentStream();
    // 0x0055F270: the animations array for `type`: <goal>_<type>, <pro>_goal_<type> or generic_pro_anims_<type> (the
    // last whether it exists or not) - its checksum; 0 without `type` when the first isn't there.
    uint32_t GetGoalAnimations(const char* type);
    // 0x0055F3D0: the trigger object (trigger_obj_id) killed (its vtable +0x18).
    void DestroyGoalPed();
    // 0x0055F420: suspended (goal_pro_stop_anim_scripts, the current stream stopped) or resumed.
    void SuspendGoalLogic(bool suspend);
    // 0x0055F460: the facial animation removed (the object's jawRotation PartialAnim) and the .fam unloaded.
    void UnloadLastFam();
    // 0x0055F5B0: the trigger object hidden (its arrow killed) or shown (its arrow added unless the goal is won);
    // not for null goals.
    void Hide(bool hide);
    // 0x0055F710: the stream's .fam (fam\<stream>.fam) loaded into `animSet`.
    void LoadFam(uint32_t stream, uint32_t animSet);
    // 0x0055F7A0: `stream` played (goal_play_stream) by the object `objectId` (0: the trigger object; skater: the
    // skater) - with its animation (and with `loadFam` its .fam) unless `noAnim`. Not while suspended or for a
    // stream that doesn't exist.
    void PlayStream(uint32_t stream, bool noAnim, uint32_t objectId, bool loadFam);
    // 0x0055F960 / 0x0055FA60: the goal's start (cam_anim_index, speaker_name, last_anim, speaker_obj_id) / win
    // (_success) stream - not while suspended (nor with no_stream).
    void PlayGoalStartStream(Script::CStruct* params);
    void PlayGoalWinStream(Script::CStruct* params);
    // 0x0055FB00: one of the pro's `count` (+20 with `callByName`) streams of `kind` in a random order:
    // <pro>_<kind><n> or, past `count`, <pro>_<kind><player><n - count> (the player being a pro).
    void PlayGoalProStream(const char* kind, int count, bool callByName, bool noAnim);
    // 0x0055FDF0: a Far stream.
    void PlayGoalWaitStream();
    // 0x0055FE10: a stream of `type` (VO: that one; play_random / call_player_by_name: the pro calling the player by
    // name, else midgoal / wait / `type`'s pro streams).
    void PlayGoalStream(const char* type, Script::CStruct* params);
};
static_assert(sizeof(CGoalPed) == 0x14, "layout must match the original");

} // namespace Game
