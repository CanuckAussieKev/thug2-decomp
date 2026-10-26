#pragma once

#include <cstddef>
#include <cstdint>

namespace Mth {
class Vector;
}
namespace Script {
class CStruct;
}

// The terrain manager (0x004442C0-0x00444EB0): the terrain types and the sound tables of terrain.qb, and the sounds
// a skater's wheels, grinds, landings... make on each terrain.
//
// Two hash tables, filled by LoadTerrainTypes from the script arrays:
// - Terrain_Types (0x006968D0): a terrain's checksum (TERRAIN_CONCSMOOTH...) -> its index, the byte collision faces
//   and rail nodes keep (an 8-byte item: the index, the checksum);
// - Terrain_Actions (0x006968E8): a sound table's index (TABLE_WHEELROLL is 0...) -> its type (TABLE_*) and action
//   (TA_ROLL...) (a 12-byte item: the index, the type, the action).
// A terrain's settings are the global structure named by its checksum (or STANDARD_TERRAIN_DEFAULT's); its sounds
// are its SoundActions: for each action (TA_*), one soundAction or an array of {useUpTo soundAction} picked by a
// value (speed...), a soundAction itself being one structure or an array of {chance ...} picked at random.
namespace Terrain {

// A terrain sound's settings (0x84 bytes; the defaults 100 except minVol 0).
struct SoundInfo {
    char name[0x68];       // +0x00 the sound's file (sound=)
    uint32_t checksum;     // +0x68 the checksum of its name without folders
    float maxPitch;        // +0x6C
    float minPitch;        // +0x70
    float maxVol;          // +0x74
    float minVol;          // +0x78
    float loadPitch;       // +0x7C
    float loadVol;         // +0x80
};
static_assert(sizeof(SoundInfo) == 0x84 && offsetof(SoundInfo, checksum) == 0x68 &&
                  offsetof(SoundInfo, loadVol) == 0x80,
              "layout must match the original");

// 0x004442C0: the index of the terrain `checksum` (one that isn't a terrain type reads address 0: a crash, as the
// original).
uint8_t __cdecl GetTerrainIndex(uint32_t checksum);
// 0x00444310: the action (TA_*) of the sound table `table` (an unknown table reads address 8: a crash).
uint32_t __cdecl GetTableAction(uint8_t table);
// 0x00444360: the checksum of the terrain with index `terrain` (0 if there's none).
uint32_t __cdecl GetTerrainChecksum(uint8_t terrain);
// 0x004443B0: the index of the sound table of type `type` (TABLE_*; 0 if there's none).
uint8_t __cdecl GetTableIndex(uint32_t type);
// 0x00444400: the structure `name` of the terrain's settings (or of STANDARD_TERRAIN_DEFAULT) into `out`; whether
// there was one. 0x00444480: the same with the arguments in another order.
bool __cdecl GetTerrainStructure(uint8_t terrain, Script::CStruct** out, uint32_t name);
bool __cdecl GetTerrainStructureInto(Script::CStruct** out, uint8_t terrain, uint32_t name);
// 0x00444460: both tables emptied (the items aren't freed - as the original).
void __cdecl ClearTerrainTypes();
// 0x004444A0: both tables filled from the script arrays Terrain_Types and Terrain_Actions.
void __cdecl LoadTerrainTypes();
// 0x004445B0: `info` from a soundAction structure (sound= and the pitch / volume settings). False without sound
// (nothing touched) or without a sound name.
bool __cdecl GetSoundInfo(Script::CStruct* soundAction, SoundInfo* info);
// 0x00444710: `percent` of the sound's volume range (minVol to maxVol; the percentage itself when that's 0-100),
// clamped to maxVol if asked. (Returned unrounded, as the original's ST0.)
double __cdecl GetScaledVolume(const SoundInfo* info, float percent, bool clamp);
// 0x00444760: the soundAction of an entry: its soundAction structure, or one of its soundAction array picked at
// random weighted by their chance (null if the pick runs out).
Script::CStruct* __cdecl PickSoundAction(Script::CStruct* entry);
// 0x00444830: the soundAction of the table's action in `soundActions`: its structure, or the first entry of its
// array whose useUpTo isn't below `value` (or has none). Null if none fits.
Script::CStruct* __cdecl FindSoundAction(Script::CStruct* soundActions, uint8_t table, float value);
// 0x004448E0: the soundAction's sound loaded (permanent, its load pitch and volume).
void __cdecl LoadSoundAction(Script::CStruct* soundAction);
// 0x00444950: every sound of the table's action in `soundActions` (or TERRAIN_DEFAULT's SoundActions) loaded.
void __cdecl LoadTableSounds(Script::CStruct* soundActions, uint8_t table);
// 0x00444B10: the sound the terrain makes for the table's action at `value`, into `info`. Whether there is one.
bool __cdecl GetTerrainSound(SoundInfo* info, uint8_t terrain, uint8_t table, float value);
// 0x00444BB0: that sound played at `position`, its volume and pitch `volumePercent` / `pitchPercent` of their
// ranges; with `propagate`, also sent to the server (message 0x35) for the other players.
void __cdecl PlayTerrainSound(uint8_t table, uint8_t terrain, const Mth::Vector* position, float volumePercent,
                              float pitchPercent, float value, bool propagate);
// 0x00444EB0: every sound of the terrain `checksum` loaded (its SoundActions merged over TERRAIN_DEFAULT's). False
// without sound.
bool __cdecl LoadTerrainSounds(uint32_t checksum);

// NOT ORIGINAL (THAW's BMX, game/skater/thaw_bmx.h): `info` played at `position` as PlayTerrainSound plays the sound it
// finds (its volume and pitch `volumePercent` / `pitchPercent` of their ranges), not sent to the other players.
void PlaySoundInfo(const SoundInfo& info, const Mth::Vector* position, float volumePercent, float pitchPercent);

} // namespace Terrain
