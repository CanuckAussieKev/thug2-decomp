#pragma once

#include <cstddef>
#include <cstdint>

// The time-of-day manager (the TODManager_* script commands): a career level's day passing through four states -
// morning, afternoon, evening, night - each a set of lighting parameters (the level's light colour, sky, fog,
// ambient and the two directional lights, the sun, bloom) that the manager blends between while time passes, and
// pushes to the engine: the scenes' and objects' colours (a few light groups and a part of the level's sectors a
// frame, so the change is spread over frames), the light manager, fog, bloom and the sun. A script (the level's
// TODManager_SetParams) gives each state's parameters and lengths; scripts are told when a state changes
// (tod_manager_state_transition).
//
// The singleton (0x007CE490, references 0x007CE494) is made by Sys::Main and updated by Mdl::Skate (0x00567710).
// From the disassembly of 0x00571140-0x00574910 (methods __thiscall, RET n = 4 * arguments; the helpers that don't
// use the manager __cdecl).
namespace Script {
class CScript;
class CStruct;
} // namespace Script
namespace Lst {
class HashTable;
}

namespace Gfx {

// One state's lighting (the parameters of a state's structure in the level's script, by the names below): 0xA4
// bytes, passed by value between the manager's methods.
struct TODParams {
    int bloomOn;            // +0x00 bloom_on (the bloom parameters are read only when it's on)
    int bloom1;             // +0x04 bloom1
    int bloom2;             // +0x08 bloom2
    uint32_t bloomStrength; // +0x0C bloom_strength [r g b]: bytes r, g, b, 0
    uint32_t bloomColor;    // +0x10 bloom_color [r g b]
    float sunTheta;         // +0x14 sun_theta (degrees in the script, kept in radians)
    float sunPhi;           // +0x18 sun_phi
    float sunRed;           // +0x1C sun_red (255 if not given)
    float sunBlue;          // +0x20 sun_blue
    float sunGreen;         // +0x24 sun_green
    int levRed;             // +0x28 lev_red: the level's (scenes' and objects') colour
    int levGreen;           // +0x2C lev_green
    int levBlue;            // +0x30 lev_blue
    int skyRed;             // +0x34 sky_red: the sky scene's colour
    int skyGreen;           // +0x38 sky_green
    int skyBlue;            // +0x3C sky_blue
    int fogOn;              // +0x40 fog_on (never read from a script; given back by GetCurrentTODParams)
    int fogRed;             // +0x44 fog_red
    int fogGreen;           // +0x48 fog_green
    int fogBlue;            // +0x4C fog_blue
    int fogAlpha;           // +0x50 fog_alpha
    int fogDist;            // +0x54 fog_dist
    int ambientRed;         // +0x58 ambient_red
    int ambientGreen;       // +0x5C ambient_green
    int ambientBlue;        // +0x60 ambient_blue
    float ambientModLo;     // +0x64 ambient_mod_factor_lo
    float ambientModHi;     // +0x68 ambient_mod_factor_hi
    float heading0;         // +0x6C heading_0: directional light 0 (degrees)
    float pitch0;           // +0x70 pitch_0
    int red0;               // +0x74 red_0
    int green0;             // +0x78 green_0
    int blue0;              // +0x7C blue_0
    float modLo0;           // +0x80 mod_factor_0_lo
    float modHi0;           // +0x84 mod_factor_0_hi
    float heading1;         // +0x88 heading_1: directional light 1
    float pitch1;           // +0x8C pitch_1
    int red1;               // +0x90 red_1
    int green1;             // +0x94 green_1
    int blue1;              // +0x98 blue_1
    float modLo1;           // +0x9C mod_factor_1_lo
    float modHi1;           // +0xA0 mod_factor_1_hi
};
static_assert(sizeof(TODParams) == 0xA4 && offsetof(TODParams, levRed) == 0x28 && offsetof(TODParams, fogOn) == 0x40 &&
                  offsetof(TODParams, heading0) == 0x6C && offsetof(TODParams, modHi1) == 0xA0,
              "layout must match the original");

// The bloom parameters as the manager passes them on (the start of TODParams; also the temporary ones).
struct BloomParams {
    int on;
    int bloom1;
    int bloom2;
    uint32_t strength;
    uint32_t color;
};
static_assert(sizeof(BloomParams) == 0x14, "the first five words of TODParams");

// The states' checksums, in the order the day goes.
constexpr uint32_t kMorning = 0x4615C1C9;   // morning
constexpr uint32_t kAfternoon = 0x577B39EE; // afternoon
constexpr uint32_t kEvening = 0x83E3976F;   // evening
constexpr uint32_t kNight = 0xEE25F068;     // night
// The state after / before `state` (0 for anything else).
uint32_t NextTODState(uint32_t state);
uint32_t PreviousTODState(uint32_t state);

// One "system" (the script's system=1 or 2): its own day - states, lengths, light groups. A sky system colours
// only the sky scene and doesn't drive the lights, fog, bloom or the scripts.
struct TODSystem {
    TODParams current;          // +0x000 (manager +0x008) the state's parameters
    TODParams next;             // +0x0A4 (+0x0AC) the next state's
    Script::CStruct* params;    // +0x148 (+0x150) a copy of the system's TODManager_SetParams parameters
    int time;                   // +0x14C (+0x154) milliseconds into the state, then into the transition
    uint32_t state;             // +0x150 (+0x158) the current state
    uint32_t nextState;         // +0x154 (+0x15C)
    int frame;                  // +0x158 (+0x160) frames since this group's last part was coloured
    int waitFrames;             // +0x15C (+0x164) the group's wait_frames: frames between its parts
    int group;                  // +0x160 (+0x168) the light group being coloured (an index into groups)
    int groupCount;             // +0x164 (+0x16C) how many groups there are
    int part;                   // +0x168 (+0x170) the part of the group being coloured
    int parts;                  // +0x16C (+0x174) the group's parts: each colours (100 / parts)% of the sectors
    uint32_t lightGroup;        // +0x170 (+0x178) the group's light_group
    uint32_t resetLightGroup;   // +0x174 (+0x17C) its reset_light_group (sectors set back to neutral)
    int transitionLength;       // +0x178 (+0x180) transition_length (ms)
    int stateLength;            // +0x17C (+0x184) the state's state_length (ms)
    int daysPassed;             // +0x180 (+0x188) nights ended
    bool transitioning;         // +0x184 (+0x18C) blending towards the next state (time counts the blend)
    bool scriptRun;             // +0x185 (+0x18D) tod_manager_state_transition run for this transition
    bool sky;                   // +0x186 (+0x18E) a sky system
    uint8_t pad187;
};
static_assert(sizeof(TODSystem) == 0x188 && offsetof(TODSystem, params) == 0x148 &&
                  offsetof(TODSystem, transitionLength) == 0x178 && offsetof(TODSystem, sky) == 0x186,
              "layout must match the original");

class CTODManager {
public:
    static constexpr int kSystems = 2;

    // 0x00573620: the constructor (returns this): the dead-objects table, the structures, then Reset.
    CTODManager* Construct();
    // 0x00573770: the destructor body.
    void Destruct();
    // 0x005730E0: back to the defaults: afternoon, states of 60 s, transitions of 30 s, active, fog on, time
    // multiplier 1; the systems' parameters and the dead-objects table emptied.
    void Reset();

    // 0x00574590: a frame (Mdl::Skate's update): in a career game, unless the script variable
    // tod_manager_inactive is 1, a temporary state is up, the manager is inactive or the front end is paused, time
    // moves on (the frame's length times the multiplier) and each system is updated.
    void Update();
    // 0x00573810: system `index`'s frame while transitioning: the next part of the current light group coloured
    // with the blended parameters (when its wait is over), the lights / fog / bloom set, the transition script
    // run half-way (at once for evening), and the next state taken when the transition ends.
    void UpdateSystem(int index);
    // 0x005723C0: system `index` moves on to its next state (a night ending counts a day).
    void NextState(int index);
    // 0x00571290: system `index`'s light group number `group` read from its params' groups array.
    void ReadGroup(int index);
    // 0x00573CE0: every system's light groups coloured at once with its current parameters, the lights set, and
    // tod_manager_state_transition run with instant_change (and temp_state with `tempState`).
    void RefreshAll(bool tempState);
    // 0x00574670: jump to `state` now (time is moved on through the states in between).
    void SetTODInstant(uint32_t state);

    // 0x00572130: TODManager_SetParams system=1|2 current_state=... <state structures> transition_length=...
    // [overwrite_current_state={...}] [sky].
    void SetParams(Script::CStruct* params);
    // 0x00571A00: the parameters in `structure` read into `out` (missing ones are left as they are).
    void ParseParams(Script::CStruct* structure, TODParams* out);
    // 0x00571340: `a` blended towards `b` by `t` (integers truncated). Fields +0x14..+0x24 and fog_on aren't set
    // by the original (stack garbage): here they're a's.
    TODParams Lerp(TODParams a, TODParams b, float t);
    // 0x00571E00: system `index`'s parameters now (blended while transitioning); -1: the first non-sky system.
    TODParams GetTODParams(int index);
    // 0x00571140: the current level colour (0x00BBGGRR) of the first non-sky system (Obj::CreateFromStructure
    // colours new objects with it).
    void GetLevelColor(uint32_t* out);
    // 0x00572A10: the parameters sent to the engine: ambient light, the two directional lights, fog (if on), the
    // level colour.
    void ApplyTODParams(TODParams params);
    // 0x005724D0: the sun (theta, phi, colour, shown unless it's night), blended through system 0's day.
    void UpdateSun();
    // 0x005727F0 (0x00572821 is the part after its first test): colour the scenes (`startPercent`..`endPercent` of
    // their sectors), the level scene and (for the outdoor group or no group) the models; or only the sky scene.
    void ApplyColor(uint32_t color, uint32_t skyColor, bool sky, uint32_t lightGroup, uint32_t resetLightGroup,
                    bool partOnly, int startPercent, int endPercent);
    // 0x00573500: bloom on (the parameters) or off.
    void SetBloom(int on, int bloom1, int bloom2, uint32_t strength, uint32_t color);
    // 0x00572320: fog on (with the current fog colour and distance) or off.
    void SetFogActive(bool on);

    // 0x00571EF0: TODManager_Create name=<node> | prefix=<prefix>: the nodes' objects made (Obj's
    // CreateFromNode), except those marked permanently dead.
    void CreateObjects(Script::CStruct* params, Script::CScript* script);
    // 0x00573400: TODManager_MarkPermanentlyDead name=<node> | prefix=<prefix>.
    void MarkPermanentlyDead(Script::CStruct* params);
    // 0x00572BD0: the dead-objects table emptied (level clean-up).
    void ClearDeadObjects();
    // 0x005732D0: SetSceneColor's colouring: color=, [sky=], light_group=, reset_light_group= ...
    void SetSceneColor(Script::CStruct* params);
    // 0x00573BE0: TODManager_ResetLevelLights [state_name=]: the lights set from the current parameters (no name)
    // or from the named state's.
    void ResetLevelLights(uint32_t stateName);
    // 0x005741B0 / 0x00574210: TODManager_SetTempTOD <params> / TODManager_RestoreCurrentTOD.
    void SetTempTOD(Script::CStruct* params);
    void RestoreCurrentTOD();
    // 0x00574270 / 0x00574400: TODManager_SetTempBloomParams / TODManager_EnableBloom.
    void SetTempBloomParams(Script::CStruct* params);
    void EnableBloom(int on);

    Lst::HashTable* m_deadObjects;       // +0x000 nodes marked permanently dead (value: the manager)
    Script::CStruct* m_unknown4;          // +0x004 made, emptied and freed, never filled
    TODSystem m_systems[kSystems];        // +0x008
    BloomParams m_tempBloom;              // +0x318 TODManager_SetTempBloomParams'
    int m_totalTime;                      // +0x32C milliseconds the manager has run
    float m_timeMultiplier;               // +0x330 TODManager_SetTimeMultiplier
    uint32_t m_transitionScript;          // +0x334 TODManager_SetTODScript: level_specific_transition_script
    bool m_fogActive;                     // +0x338
    bool m_levelLightsReset;              // +0x339 ResetLevelLights with a state: the lights aren't blended
    bool m_tempTOD;                       // +0x33A a temporary state is up (time stops)
    bool m_active;                        // +0x33B TODManager_SetActive
    bool m_paramsSet;                     // +0x33C
    uint8_t m_pad33D[3];

private:
    // The first system that isn't a sky system (index 2 - past the end - if both are; never checked).
    int CurrentSystem() const;
};
static_assert(offsetof(CTODManager, m_systems) == 8 && offsetof(CTODManager, m_tempBloom) == 0x318 &&
                  offsetof(CTODManager, m_timeMultiplier) == 0x330 && offsetof(CTODManager, m_paramsSet) == 0x33C &&
                  sizeof(CTODManager) == 0x340,
              "layout must match the original (0x00574540 allocates 0x340 bytes)");

// 0x005725F0: `color` given to `scene`'s sectors (the scene's sector table: those of `lightGroup` - and those of
// `resetLightGroup` set back to neutral - or every sector for no group), only startPercent..endPercent of them
// when startPercent >= 0.
void ApplyColorToScene(uint8_t* scene, uint32_t color, uint32_t lightGroup, uint32_t resetLightGroup,
                       int startPercent, int endPercent);
// 0x00572980: directional light `index` pointed by heading and pitch (degrees).
void SetLightDirection(int index, float heading, float pitch);
// 0x00573180: the bloom parameters added to `out` (on, bloom1, bloom2, strength, color).
void AddBloomParams(int on, int bloom1, int bloom2, uint32_t strength, uint32_t color, Script::CStruct* out);

// The singleton (0x007CE490).
CTODManager*& TODManager();
// 0x00574540: the singleton (made zeroed with `create` if there's none); a reference taken.
CTODManager* CreateTODManager(bool create);
// 0x00574910: a reference dropped; the last one deletes the manager.
void ReleaseTODManager();

// The script commands (0x00571FE0-0x00574870).
bool __cdecl CFunc_TODManager_SetActive(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_TODManager_GetNumberOfDaysPassed(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_TODManager_SetTimeMultiplier(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_TODManager_GetTimeMultiplier(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_TODManager_SetTODScript(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_TODManager_Create(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_TODManager_SetParams(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_TODManager_GetCurrentTODParams(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_TODManager_SetFogActive(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_TODManager_MarkPermanentlyDead(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_TODManager_Reset(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_TODManager_ResetLevelLights(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_TODManager_SetTempTOD(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_TODManager_RestoreCurrentTOD(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_TODManager_SetTempBloomParams(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_TODManager_EnableBloom(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_TODManager_SetTODInstant(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_TODManager_AdvanceTODState(Script::CStruct* params, Script::CScript* script);

} // namespace Gfx
