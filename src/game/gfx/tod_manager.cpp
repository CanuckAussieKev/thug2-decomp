// The time-of-day manager and its script commands. See tod_manager.h.

#include "game/gfx/tod_manager.h"

#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/lst/hashtable.h"
#include "game/obj/compositemgr.h"
#include "game/script/cscript.h"
#include "game/script/node_prefixes.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Gfx {

namespace {

// Names.
constexpr uint32_t kName = 0xA1DC81F9;                    // name
constexpr uint32_t kPrefix = 0x6C4E7971;                  // prefix
constexpr uint32_t kNoPrefixWarning = 0x80804991;         // NoPrefixWarning
constexpr uint32_t kSystem = 0x36B2EE74;                  // system
constexpr uint32_t kSystemIndex = 0x2B430364;             // system_index
constexpr uint32_t kCurrentState = 0xF8725A00;            // current_state
constexpr uint32_t kOverwriteCurrentState = 0x6136E863;   // overwrite_current_state
constexpr uint32_t kStateLength = 0xF746BEE2;             // state_length
constexpr uint32_t kTransitionLength = 0x37BC278D;        // transition_length
constexpr uint32_t kSky = 0xF9D98B10;                     // sky
constexpr uint32_t kGroups = 0x0F92C68F;                  // groups
constexpr uint32_t kParts = 0x96BF5801;                   // parts
constexpr uint32_t kWaitFrames = 0x3FAF7F7F;              // wait_frames
constexpr uint32_t kLightGroup = 0x9655CFF7;              // light_group
constexpr uint32_t kResetLightGroup = 0xEC50037D;         // reset_light_group
constexpr uint32_t kNumDaysPassed = 0x49EF4348;           // num_days_passed
constexpr uint32_t kTimeMultiplierName = 0x1868AFB2;      // TOD_Time_Multiplier
constexpr uint32_t kActive = 0xB4E103FD;                  // active
constexpr uint32_t kCurrentStateName = 0xEA84C14D;        // current_state_name
constexpr uint32_t kTodParams = 0xDF3D6451;               // tod_params
constexpr uint32_t kStateName = 0x353F083F;               // state_name
constexpr uint32_t kTempTODChanged = 0x4AF50A5B;          // todmanager_temp_tod_changed (a script global)
constexpr uint32_t kInactive = 0xAAE2353A;                // tod_manager_inactive (a script global)
constexpr uint32_t kCareer = 0x4DA4937B;                  // career
constexpr uint32_t kCurrentTod = 0xF200CB7E;              // current_tod
constexpr uint32_t kNextTod = 0x053D35AF;                 // next_tod
constexpr uint32_t kTodStateParams = 0x48F1380F;          // tod_state_params
constexpr uint32_t kTransitionScript = 0x37D7E331;        // level_specific_transition_script
constexpr uint32_t kStateTransition = 0xD99EE043;         // tod_manager_state_transition (a script)
constexpr uint32_t kInstantChange = 0x43633E05;           // instant_change
constexpr uint32_t kTempState = 0xCACC8C05;               // temp_state
constexpr uint32_t kBloomParams = 0x19C45D14;             // bloom_params
constexpr uint32_t kOn = 0xF649D637;                      // on
constexpr uint32_t kBloom1 = 0xF68014D9;                  // bloom1
constexpr uint32_t kBloom2 = 0x6F894563;                  // bloom2
constexpr uint32_t kStrength = 0x29461E27;                // strength
constexpr uint32_t kColor = 0x99A9B716;                   // color
constexpr uint32_t kOutdoor = 0xE3714DC1;                 // outdoor (the light group of everything outside)
constexpr uint32_t kModel = 0x286A8D26;                   // model (the component type)
// SetSceneColor's other parameters (names not recovered).
constexpr uint32_t kSceneColorFlag = 0xE5B6C568;          // the sectors only (not the scenes' own colour, the models)
constexpr uint32_t kSceneColorStart = 0xDF1A695F;         // the first percent of the sectors
constexpr uint32_t kSceneColorEnd = 0x8F639F94;           // the last percent

// The parameters' names, in the order of TODParams from +0x28 (fog_on, +0x40, isn't read from a script).
constexpr uint32_t kLevRed = 0x966014FF, kLevGreen = 0x5F727131, kLevBlue = 0x1C5D554E;
constexpr uint32_t kSkyRed = 0xBFAC0E79, kSkyGreen = 0xCFC99E4A, kSkyBlue = 0x18AFBF41;
constexpr uint32_t kFogOn = 0x028CCF87, kFogRed = 0xCE0BD6E5, kFogGreen = 0x9A35701D, kFogBlue = 0xE167C7F6;
constexpr uint32_t kFogAlpha = 0x9A4FA756, kFogDist = 0xFE2DDDB5;
constexpr uint32_t kAmbientRed = 0xED664897, kAmbientGreen = 0x908BF647, kAmbientBlue = 0x5F4FBA78;
constexpr uint32_t kAmbientModLo = 0xB3B5E2EF, kAmbientModHi = 0x3EBA82DE;
constexpr uint32_t kHeading0 = 0xC15B55B1, kPitch0 = 0xD9FF5B75;
constexpr uint32_t kRed0 = 0xE213AEB7, kGreen0 = 0x6EF19631, kBlue0 = 0x5EC44213;
constexpr uint32_t kModLo0 = 0x05999531, kModHi0 = 0x8896F500;
constexpr uint32_t kHeading1 = 0xB65C6527, kPitch1 = 0xAEF86BE3;
constexpr uint32_t kRed1 = 0x95149E21, kGreen1 = 0x19F6A6A7, kBlue1 = 0x29C37285;
constexpr uint32_t kModLo1 = 0xBD25F254, kModHi1 = 0x302A9265;
constexpr uint32_t kSunTheta = 0xDF037B3B, kSunPhi = 0x9C2BF09E;
constexpr uint32_t kSunRed = 0x54B02600, kSunGreen = 0x04811F13, kSunBlue = 0x319D6AF1;
constexpr uint32_t kBloomOn = 0x400681B6, kBloomStrength = 0x743D0048, kBloomColor = 0x6AC75185;

// A sector's colour set back to neutral (r, g, b 0x80, alpha 0x81).
constexpr uint32_t kNeutralColor = 0x81808080;

// The exe's constants.
float DegreesToRadians() { return GameVariable<float>(0x00645464); } // 0.017453292
float Zero() { return GameVariable<float>(0x0066D520); }             // 0.0
float OnePercent() { return GameVariable<float>(0x0064544C); }       // 0.01
float OneOver255() { return GameVariable<float>(0x00645D70); }       // 1 / 255
float TwoTo32() { return GameVariable<float>(0x006453B4); }          // 4294967296: unsigned integers' correction
float One() { return GameVariable<float>(0x006453A8); }              // 1.0
float Half() { return GameVariable<float>(0x006453BC); }             // 0.5
float Hundred() { return GameVariable<float>(0x006463D4); }          // 100.0
float Thousand() { return GameVariable<float>(0x006453C0); }         // 1000.0

// The C runtime's _ftol (0x006236FC): truncation, the low 32 bits of the 64-bit result.
int Ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The x87's own FSIN / FCOS, which the original uses (the C runtime's sin and cos round differently).
double X87Sin(double angle)
{
    double result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}
double X87Cos(double angle)
{
    double result;
    __asm {
        fld angle
        fcos
        fstp result
    }
    return result;
}
// The same, stored straight to a float (rounded once).
float X87SinFloat(double angle)
{
    float result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}
float X87CosFloat(double angle)
{
    float result;
    __asm {
        fld angle
        fcos
        fstp result
    }
    return result;
}

// An unsigned millisecond count over an int length, as the original divides (FILD, + 2^32 if negative, FIDIV).
double Fraction(int time, int length)
{
    double value = static_cast<double>(time);
    if (time < 0)
        value += TwoTo32();
    return value / static_cast<double>(length);
}

// A colour from three bytes (and the fourth).
uint32_t Rgba(int r, int g, int b, uint8_t a)
{
    return static_cast<uint8_t>(r) | static_cast<uint8_t>(g) << 8 | static_cast<uint8_t>(b) << 16 |
           static_cast<uint32_t>(a) << 24;
}
// A colour from a script array [r g b] (elements truncated to bytes; the fourth byte 0).
uint32_t ColorFromArray(Script::CArray* array)
{
    uint8_t r = static_cast<uint8_t>(array->GetElement(0));
    uint8_t g = static_cast<uint8_t>(array->GetElement(1));
    uint8_t b = static_cast<uint8_t>(array->GetElement(2));
    return Rgba(r, g, b, 0);
}

Script::CStruct* NewStructure()
{
    Script::CStruct* memory = Script::NewStruct();
    return memory ? memory->Init() : nullptr;
}
void DeleteStructure(Script::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Script::DeleteStruct(structure);
    }
}

// The engine (not rewritten yet).
void SetFog(bool on) { GameFunction<void(__cdecl*)(int)>(0x00490DE0)(on); }
void SetFogColor(uint32_t color) { GameFunction<void(__cdecl*)(uint32_t)>(0x00490E20)(color); }
void SetFogDistance(float distance) { GameFunction<void(__cdecl*)(float)>(0x00490E00)(distance); }
void SetAmbientColor(uint32_t color) { GameFunction<void(__cdecl*)(uint32_t)>(0x0048E8F0)(color); }
void SetLightColor(int index, uint32_t color) { GameFunction<void(__cdecl*)(int, uint32_t)>(0x0048E930)(index, color); }
void SetAmbientModulation(float lo, float hi) { GameFunction<void(__cdecl*)(float, float)>(0x0048E970)(lo, hi); }
void SetLightModulation(int index, float lo, float hi)
{
    GameFunction<void(__cdecl*)(int, float, float)>(0x0048E990)(index, lo, hi);
}
void SetLightDirectionVector(int index, const float* direction)
{
    GameFunction<void(__cdecl*)(int, const float*)>(0x0048ED70)(index, direction);
}
// The sun: its parameters (theta +0x00, phi +0x04, colour +0x18 as r g b 128, shown +0x40) by mask.
struct SunParams {
    float theta;
    float phi;
    uint32_t unknown08[4];
    float color[4];
    uint32_t unknown28[6];
    bool shown;
    uint8_t pad41[0xF];
};
static_assert(offsetof(SunParams, color) == 0x18 && offsetof(SunParams, shown) == 0x40, "0x004426D0's layout");
constexpr int kSunAngles = 0x01 | 0x02, kSunColor = 0x10, kSunShown = 0x80;
void SetSun(const SunParams* sun, int mask) { GameFunction<void(__cdecl*)(const SunParams*, int)>(0x004426D0)(sun, mask); }
// The level colour as the renderer's material colour (0x006FA7C4's, 0x006FA7C0's, and a global copy).
void SetLevelColor(const float* color)
{
    GameFunction<void(__thiscall*)(void*, const float*)>(0x004966B0)(GameVariable<void*>(0x006FA7C4), color);
    GameFunction<void(__thiscall*)(void*, const float*)>(0x0049AE80)(GameVariable<void*>(0x006FA7C0), color);
    GameFunction<void(__cdecl*)(const float*)>(0x00491290)(color);
}
void SetBloomStructure(Script::CStruct* params)
{
    GameFunction<void(__thiscall*)(void*, Script::CStruct*, int)>(0x0049E360)(GameVariable<void*>(0x006FA7CC), params,
                                                                              0);
}
// Whether this is a multiplayer game (a network game, or more than one local player): the lights stay as set.
bool IsMultiplayer() { return GameFunction<bool(__cdecl*)()>(0x005651F0)() & 1; }
// The game mode's name (Mdl::Skate's game mode 0x007CE470).
uint32_t GameModeName()
{
    return GameFunction<uint32_t(__thiscall*)(void*)>(0x005532A0)(GameVariable<void*>(0x007CE470));
}
// The front end (0x007CE464): +0x30 paused.
bool GamePaused() { return GameVariable<uint8_t*>(0x007CE464)[0x30] != 0; }
// Obj's CreateFromNode (0x005A32A0): the object of node `name` made.
void CreateFromNode(uint32_t name) { GameFunction<void(__cdecl*)(uint32_t)>(0x005A32A0)(name); }

// The scenes (Nx::CEngine's list 0x006FA7EC): nodes {?, scene +4, ?, next +0xC}, ended by a node whose scene is -1.
struct SceneNode {
    uint32_t unknown0;
    uint8_t* scene;
    uint32_t unknown8;
    SceneNode* next;
};
bool IsEnd(const SceneNode* node) { return reinterpret_cast<intptr_t>(node->scene) == -1; }
SceneNode* FirstScene() { return GameFunction<SceneNode*(__cdecl*)()>(0x0048C7F0)(); }
uint8_t* SkyScene() { return GameFunction<uint8_t*(__cdecl*)()>(0x0048C7B0)(); }
// The scene of the level being played (0x0058D710's checksum, looked up by 0x0048C700).
uint8_t* LevelScene()
{
    uint32_t name = GameFunction<uint32_t(__cdecl*)()>(0x0058D710)();
    return GameFunction<uint8_t*(__cdecl*)(uint32_t)>(0x0048C700)(name);
}
// A scene's own colour (vtable +0x20).
void SetSceneColorOf(uint8_t* scene, uint32_t color)
{
    auto** vtable = *reinterpret_cast<void***>(scene);
    reinterpret_cast<void(__thiscall*)(uint8_t*, uint32_t)>(vtable[8])(scene, color);
}
// The sector named `name` in any scene (0x0048C510): +0x0C its light group.
uint8_t* GetSector(uint32_t name) { return GameFunction<uint8_t*(__cdecl*)(uint32_t)>(0x0048C510)(name); }
// A model component's colour (0x00420700).
void SetModelColor(Obj::CBaseComponent* model, uint32_t color)
{
    GameFunction<void(__thiscall*)(Obj::CBaseComponent*, uint32_t)>(0x00420700)(model, color);
}

TODSystem& SystemAt(CTODManager* manager, int index)
{
    // (no bounds check: index 2 is past the end, as in the original)
    return *reinterpret_cast<TODSystem*>(reinterpret_cast<uint8_t*>(manager->m_systems) + index * sizeof(TODSystem));
}

BloomParams BloomOf(const TODParams& params)
{
    return {params.bloomOn, params.bloom1, params.bloom2, params.bloomStrength, params.bloomColor};
}

} // namespace

uint32_t NextTODState(uint32_t state)
{
    switch (state) {
    case kMorning:
        return kAfternoon;
    case kAfternoon:
        return kEvening;
    case kEvening:
        return kNight;
    case kNight:
        return kMorning;
    default:
        return 0;
    }
}

uint32_t PreviousTODState(uint32_t state)
{
    switch (state) {
    case kMorning:
        return kNight;
    case kAfternoon:
        return kMorning;
    case kEvening:
        return kAfternoon;
    case kNight:
        return kEvening;
    default:
        return 0;
    }
}

int CTODManager::CurrentSystem() const
{
    int index = 0;
    while (index < kSystems && m_systems[index].sky)
        ++index;
    return index;
}

// 0x00571140
void CTODManager::GetLevelColor(uint32_t* out)
{
    TODSystem& system = SystemAt(this, CurrentSystem());
    if (!system.transitioning) {
        const TODParams& p = system.current;
        *out = (((p.levBlue << 8) + p.levGreen) << 8) + p.levRed;
        return;
    }
    double t = 1.0;
    if (system.transitionLength) {
        t = Fraction(system.time, system.transitionLength);
        if (!(t <= One()))
            t = One();
    }
    const TODParams& a = system.current;
    const TODParams& b = system.next;
    int blue = Ftol(static_cast<double>(b.levBlue - a.levBlue) * t + a.levBlue);
    int green = Ftol(static_cast<double>(b.levGreen - a.levGreen) * t + a.levGreen);
    int red = Ftol(static_cast<double>(b.levRed - a.levRed) * t + a.levRed);
    *out = (((blue << 8) + green) << 8) + red;
}

// 0x00571290
void CTODManager::ReadGroup(int index)
{
    TODSystem& system = SystemAt(this, index);
    Script::CArray* groups;
    system.params->GetArray(kGroups, &groups, 1);
    system.groupCount = static_cast<int>(groups->m_size);
    auto* group = reinterpret_cast<Script::CStruct*>(groups->GetElement(static_cast<uint32_t>(system.group)));
    group->GetInteger(kParts, &system.parts, 1);
    group->GetInteger(kWaitFrames, &system.waitFrames, 1);
    group->GetChecksum(kLightGroup, &system.lightGroup, 1);
    if (!group->GetChecksum(kResetLightGroup, &system.resetLightGroup, 0))
        system.resetLightGroup = 0;
}

// 0x00571340
TODParams CTODManager::Lerp(TODParams a, TODParams b, float t)
{
    auto integer = [t](int from, int to) { return Ftol(static_cast<double>(to - from) * t + from); };
    auto real = [t](float from, float to) {
        return static_cast<float>((static_cast<double>(to) - from) * t + from);
    };
    auto byte = [&](uint32_t from, uint32_t to, int shift) {
        int f = static_cast<int>(from >> shift & 0xFF);
        int g = static_cast<int>(to >> shift & 0xFF);
        return static_cast<uint32_t>(static_cast<uint8_t>(integer(f, g))) << shift;
    };
    TODParams r = a; // (+0x14..+0x24 and fog_on: not set by the original - stack garbage)
    r.levRed = integer(a.levRed, b.levRed);
    r.levGreen = integer(a.levGreen, b.levGreen);
    r.levBlue = integer(a.levBlue, b.levBlue);
    r.skyRed = integer(a.skyRed, b.skyRed);
    r.skyGreen = integer(a.skyGreen, b.skyGreen);
    r.skyBlue = integer(a.skyBlue, b.skyBlue);
    r.fogRed = integer(a.fogRed, b.fogRed);
    r.fogGreen = integer(a.fogGreen, b.fogGreen);
    r.fogBlue = integer(a.fogBlue, b.fogBlue);
    r.fogAlpha = integer(a.fogAlpha, b.fogAlpha);
    r.fogDist = integer(a.fogDist, b.fogDist);
    r.ambientRed = integer(a.ambientRed, b.ambientRed);
    r.ambientGreen = integer(a.ambientGreen, b.ambientGreen);
    r.ambientBlue = integer(a.ambientBlue, b.ambientBlue);
    r.ambientModLo = real(a.ambientModLo, b.ambientModLo);
    r.ambientModHi = real(a.ambientModHi, b.ambientModHi);
    r.heading0 = real(a.heading0, b.heading0);
    r.pitch0 = real(a.pitch0, b.pitch0);
    r.red0 = integer(a.red0, b.red0);
    r.green0 = integer(a.green0, b.green0);
    r.blue0 = integer(a.blue0, b.blue0);
    r.modLo0 = real(a.modLo0, b.modLo0);
    r.modHi0 = real(a.modHi0, b.modHi0);
    r.heading1 = real(a.heading1, b.heading1);
    r.pitch1 = real(a.pitch1, b.pitch1);
    r.red1 = integer(a.red1, b.red1);
    r.green1 = integer(a.green1, b.green1);
    r.blue1 = integer(a.blue1, b.blue1);
    r.modLo1 = real(a.modLo1, b.modLo1);
    r.modHi1 = real(a.modHi1, b.modHi1);
    r.bloomOn = a.bloomOn;
    r.bloom1 = integer(a.bloom1, b.bloom1);
    r.bloom2 = integer(a.bloom2, b.bloom2);
    // The bloom colours byte by byte; their fourth byte is 0x80.
    r.bloomStrength = byte(a.bloomStrength, b.bloomStrength, 0) | byte(a.bloomStrength, b.bloomStrength, 8) |
                      byte(a.bloomStrength, b.bloomStrength, 16) | 0x80000000u;
    r.bloomColor = byte(a.bloomColor, b.bloomColor, 0) | byte(a.bloomColor, b.bloomColor, 8) |
                   byte(a.bloomColor, b.bloomColor, 16) | 0x80000000u;
    return r;
}

// 0x00571A00
void CTODManager::ParseParams(Script::CStruct* structure, TODParams* out)
{
    structure->GetInteger(kLevRed, &out->levRed, 0);
    structure->GetInteger(kLevGreen, &out->levGreen, 0);
    structure->GetInteger(kLevBlue, &out->levBlue, 0);
    structure->GetInteger(kSkyRed, &out->skyRed, 0);
    structure->GetInteger(kSkyGreen, &out->skyGreen, 0);
    structure->GetInteger(kSkyBlue, &out->skyBlue, 0);
    structure->GetInteger(kFogRed, &out->fogRed, 0);
    structure->GetInteger(kFogGreen, &out->fogGreen, 0);
    structure->GetInteger(kFogBlue, &out->fogBlue, 0);
    structure->GetInteger(kFogAlpha, &out->fogAlpha, 0);
    structure->GetInteger(kFogDist, &out->fogDist, 0);
    structure->GetInteger(kAmbientRed, &out->ambientRed, 0);
    structure->GetInteger(kAmbientGreen, &out->ambientGreen, 0);
    structure->GetInteger(kAmbientBlue, &out->ambientBlue, 0);
    structure->GetFloat(kAmbientModLo, &out->ambientModLo, 0);
    structure->GetFloat(kAmbientModHi, &out->ambientModHi, 0);
    structure->GetFloat(kHeading0, &out->heading0, 0);
    structure->GetFloat(kPitch0, &out->pitch0, 0);
    structure->GetInteger(kRed0, &out->red0, 0);
    structure->GetInteger(kGreen0, &out->green0, 0);
    structure->GetInteger(kBlue0, &out->blue0, 0);
    structure->GetFloat(kModLo0, &out->modLo0, 0);
    structure->GetFloat(kModHi0, &out->modHi0, 0);
    structure->GetFloat(kHeading1, &out->heading1, 0);
    structure->GetFloat(kPitch1, &out->pitch1, 0);
    structure->GetInteger(kRed1, &out->red1, 0);
    structure->GetInteger(kGreen1, &out->green1, 0);
    structure->GetInteger(kBlue1, &out->blue1, 0);
    structure->GetFloat(kModLo1, &out->modLo1, 0);
    structure->GetFloat(kModHi1, &out->modHi1, 0);

    float value = 0.0f;
    if (structure->GetFloat(kSunTheta, &value, 0))
        out->sunTheta = static_cast<float>(static_cast<double>(value) * DegreesToRadians());
    if (structure->GetFloat(kSunPhi, &value, 0))
        out->sunPhi = static_cast<float>(static_cast<double>(value) * DegreesToRadians());
    out->sunRed = 255.0f;
    out->sunGreen = 255.0f;
    out->sunBlue = 255.0f;
    if (structure->GetFloat(kSunRed, &value, 0))
        out->sunRed = value;
    if (structure->GetFloat(kSunGreen, &value, 0))
        out->sunGreen = value;
    if (structure->GetFloat(kSunBlue, &value, 0))
        out->sunBlue = value;

    structure->GetInteger(kBloomOn, &out->bloomOn, 0);
    if (!out->bloomOn)
        return;
    structure->GetInteger(kBloom1, &out->bloom1, 0);
    structure->GetInteger(kBloom2, &out->bloom2, 0);
    Script::CArray* array = nullptr;
    if (structure->GetArray(kBloomStrength, &array, 0))
        out->bloomStrength = ColorFromArray(array);
    array = nullptr;
    if (structure->GetArray(kBloomColor, &array, 0))
        out->bloomColor = ColorFromArray(array);
}

// 0x00571E00
TODParams CTODManager::GetTODParams(int index)
{
    if (index == -1)
        index = CurrentSystem();
    TODSystem& system = SystemAt(this, index);
    if (!system.transitioning)
        return system.current;
    float t = 1.0f;
    if (system.transitionLength) {
        double fraction = Fraction(system.time, system.transitionLength);
        t = static_cast<float>(fraction);
        if (!(fraction <= One()))
            t = 1.0f;
    }
    return Lerp(system.current, system.next, t);
}

// 0x00571EF0
void CTODManager::CreateObjects(Script::CStruct* params, Script::CScript* /*script*/)
{
    uint32_t name = 0;
    if (params->GetChecksum(kName, &name, 0)) {
        if (!m_deadObjects->GetItem(name, true))
            CreateFromNode(name);
        return;
    }
    uint32_t prefix;
    if (!params->GetChecksumOrStringChecksum(kPrefix, &prefix, 0))
        return;
    // (NoPrefixWarning: passed on as the third argument of 0x0045BF90, which doesn't use it)
    params->ContainsFlag(kNoPrefixWarning);
    uint16_t count;
    uint32_t* nodes = SkateScript::GetPrefixedNodes(prefix, &count);
    for (int i = 0; i < count; ++i) {
        if (!m_deadObjects->GetItem(nodes[i]))
            CreateFromNode(nodes[i]);
    }
}

// 0x00572130
void CTODManager::SetParams(Script::CStruct* params)
{
    m_paramsSet = true;
    int number;
    params->GetInteger(kSystem, &number, 1);
    TODSystem& system = SystemAt(this, number - 1);
    system.params->Clear();
    system.params->AppendStructureIfAny(params);
    params->GetChecksum(kCurrentState, &system.state, 0);
    system.nextState = NextTODState(system.state);
    Script::CStruct* state;
    if (params->GetStructure(kOverwriteCurrentState, &state, 0)) {
        ParseParams(state, &system.current);
    } else if (params->GetStructure(system.state, &state, 0)) {
        ParseParams(state, &system.current);
        state->GetInteger(kStateLength, &system.stateLength, 1);
    }
    if (params->GetStructure(system.nextState, &state, 0))
        ParseParams(state, &system.next);
    params->GetInteger(kTransitionLength, &system.transitionLength, 0);
    system.sky = params->ContainsFlag(kSky);
}

// 0x00572320
void CTODManager::SetFogActive(bool on)
{
    bool was = m_fogActive;
    m_fogActive = on;
    if (was) {
        if (!on)
            SetFog(false);
        return;
    }
    if (!on)
        return;
    SetFog(true);
    TODParams params = GetTODParams(-1);
    SetFogColor(Rgba(params.fogRed, params.fogGreen, params.fogBlue, static_cast<uint8_t>(params.fogAlpha)));
    SetFogDistance(static_cast<float>(params.fogDist));
}

// 0x005723C0
void CTODManager::NextState(int index)
{
    TODSystem& system = SystemAt(this, index);
    if (system.state == kNight)
        ++system.daysPassed;
    system.state = system.nextState;
    system.nextState = NextTODState(system.state);
    Script::CStruct* state;
    system.params->GetStructure(system.state, &state, 1);
    ParseParams(state, &system.current);
    state->GetInteger(kStateLength, &system.stateLength, 1);
    system.params->GetStructure(system.nextState, &state, 1);
    ParseParams(state, &system.next);
    system.scriptRun = false;
    system.time = 0;
    system.part = 0;
    system.group = 0;
    system.frame = 0;
    system.transitioning = false;
}

// 0x005724D0
void CTODManager::UpdateSun()
{
    TODSystem& system = m_systems[0];
    double t;
    if (system.transitioning)
        t = Fraction(system.time + system.stateLength, system.transitionLength + system.stateLength);
    else
        t = Fraction(system.time, system.stateLength + system.transitionLength);
    auto blend = [t](float from, float to) {
        return static_cast<float>((static_cast<double>(to) - from) * t + from);
    };
    const TODParams& a = system.current;
    const TODParams& b = system.next;
    SunParams sun = {};
    sun.theta = blend(a.sunTheta, b.sunTheta);
    sun.phi = blend(a.sunPhi, b.sunPhi);
    sun.color[0] = blend(a.sunRed, b.sunRed);
    sun.color[1] = blend(a.sunGreen, b.sunGreen);
    sun.color[2] = blend(a.sunBlue, b.sunBlue);
    sun.color[3] = 128.0f;
    sun.shown = system.state != kNight;
    SetSun(&sun, kSunAngles | kSunColor | kSunShown);
}

// 0x005725F0
void ApplyColorToScene(uint8_t* scene, uint32_t color, uint32_t lightGroup, uint32_t resetLightGroup, int startPercent,
                       int endPercent)
{
    // The scene's sectors: +0x0C the light group, +0x14 the geometry (vtable +0x18: its colour).
    auto* table = *reinterpret_cast<Lst::HashTable**>(scene + 0xD0);
    if (!table)
        return;
    auto colour = [](uint8_t* sector, uint32_t value) {
        void* geometry = *reinterpret_cast<void**>(sector + 0x14);
        if (geometry) {
            auto** vtable = *reinterpret_cast<void***>(geometry);
            reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(vtable[6])(geometry, value);
        }
    };
    auto next = [table]() -> uint8_t* {
        if (!table->IterateNext() || !table->m_iterItem)
            return nullptr;
        return static_cast<uint8_t*>(table->m_iterItem->value);
    };
    table->m_iterBucket = -1;
    table->m_iterItem = nullptr;
    uint8_t* sector = nullptr;
    if (table->IterateNext() && table->m_iterItem)
        sector = static_cast<uint8_t*>(table->m_iterItem->value);
    int count = table->m_size;
    int remaining = count;
    if (startPercent >= 0) {
        int skip = Ftol(static_cast<double>(startPercent) * count * OnePercent());
        if (startPercent < endPercent) {
            int last = Ftol(static_cast<double>(endPercent) * count * OnePercent());
            if (count < last)
                last = count;
            remaining = last - skip;
        }
        for (int i = skip; i != 0; --i)
            sector = next();
    }
    if (!sector)
        return;
    if (!lightGroup) {
        do {
            if (remaining-- == 0)
                return;
            colour(sector, color);
        } while ((sector = next()) != nullptr);
    } else if (!resetLightGroup) {
        do {
            if (remaining-- == 0)
                return;
            if (*reinterpret_cast<uint32_t*>(sector + 0xC) == lightGroup)
                colour(sector, color);
        } while ((sector = next()) != nullptr);
    } else {
        do {
            if (remaining-- == 0)
                return;
            uint32_t group = *reinterpret_cast<uint32_t*>(sector + 0xC);
            if (group == lightGroup)
                colour(sector, color);
            else if (group == resetLightGroup)
                colour(sector, kNeutralColor);
        } while ((sector = next()) != nullptr);
    }
}

// 0x005727F0 (and 0x00572821)
void CTODManager::ApplyColor(uint32_t color, uint32_t skyColor, bool sky, uint32_t lightGroup, uint32_t resetLightGroup,
                             bool partOnly, int startPercent, int endPercent)
{
    if (sky) {
        uint8_t* scene = SkyScene();
        if (scene)
            ApplyColorToScene(scene, skyColor, lightGroup, 0, -1, -1);
        return;
    }
    bool wholeLevel = lightGroup == kOutdoor || lightGroup == 0;
    for (SceneNode* node = FirstScene(); node;) {
        SceneNode* next = IsEnd(node->next) ? nullptr : node->next;
        uint8_t* scene = node->scene;
        if (!scene[0x8D]) { // (skies: +0x8D)
            ApplyColorToScene(scene, color, lightGroup, resetLightGroup, startPercent, endPercent);
            if (wholeLevel && !partOnly)
                SetSceneColorOf(scene, color);
        }
        node = next;
    }
    uint8_t* level = LevelScene();
    if (level) {
        ApplyColorToScene(level, color, lightGroup, resetLightGroup, startPercent, endPercent);
        if (wholeLevel && !partOnly)
            SetSceneColorOf(level, color);
    }
    if ((wholeLevel || resetLightGroup) && !partOnly) {
        // The models: those of objects without a sector of their name, or an outdoor one, get the colour.
        for (Obj::CBaseComponent* model = Obj::CompositeObjectManager()->GetFirstComponentByType(kModel); model;
             model = *reinterpret_cast<Obj::CBaseComponent**>(reinterpret_cast<uint8_t*>(model) + 0x14)) {
            uint8_t* object = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(model) + 0xC);
            uint8_t* sector = GetSector(*reinterpret_cast<uint32_t*>(object + 0x14));
            if (!sector || *reinterpret_cast<uint32_t*>(sector + 0xC) == kOutdoor)
                SetModelColor(model, color);
            if (resetLightGroup && sector && *reinterpret_cast<uint32_t*>(sector + 0xC) == resetLightGroup)
                SetModelColor(model, kNeutralColor);
        }
    }
}

// 0x00572980
void SetLightDirection(int index, float heading, float pitch)
{
    double p = static_cast<double>(pitch) * DegreesToRadians();
    double sinPitch = X87Sin(p);
    double cosPitch = X87Cos(p);
    float direction[4];
    direction[3] = 0.0f;
    direction[1] = static_cast<float>(static_cast<double>(Zero()) * cosPitch - sinPitch);
    double horizontal = cosPitch + sinPitch * Zero();
    double h = static_cast<double>(heading) * DegreesToRadians();
    float sinHeading = X87SinFloat(h);
    double cosHeading = X87Cos(h);
    float cosHeadingFloat = X87CosFloat(h);
    direction[2] = static_cast<float>(cosHeading * horizontal + static_cast<double>(sinHeading) * Zero());
    direction[0] = static_cast<float>(horizontal * sinHeading + static_cast<double>(cosHeadingFloat) * Zero());
    SetLightDirectionVector(index, direction);
}

// 0x00572A10
void CTODManager::ApplyTODParams(TODParams params)
{
    SetAmbientColor(Rgba(params.ambientRed, params.ambientGreen, params.ambientBlue, 0x80));
    SetLightDirection(0, params.heading0, params.pitch0);
    SetLightDirection(1, params.heading1, params.pitch1);
    SetLightColor(0, Rgba(params.red0, params.green0, params.blue0, 0x80));
    SetLightColor(1, Rgba(params.red1, params.green1, params.blue1, 0x80));
    SetAmbientModulation(params.ambientModLo, params.ambientModHi);
    SetLightModulation(0, params.modLo0, params.modHi0);
    SetLightModulation(1, params.modLo1, params.modHi1);
    if (m_fogActive) {
        SetFog(true);
        SetFogColor(Rgba(params.fogRed, params.fogGreen, params.fogBlue, static_cast<uint8_t>(params.fogAlpha)));
        SetFogDistance(static_cast<float>(params.fogDist));
    }
    // The level colour, 0..255 as 0..2.
    auto channel = [](int value) {
        double scaled = static_cast<double>(value) * OneOver255();
        return static_cast<float>(scaled + scaled);
    };
    float color[4] = {channel(params.levRed), channel(params.levGreen), channel(params.levBlue), 1.0f};
    SetLevelColor(color);
}

// 0x00572BD0
void CTODManager::ClearDeadObjects() { m_deadObjects->FlushAllItems(); }

// 0x005730E0
void CTODManager::Reset()
{
    m_paramsSet = false;
    m_totalTime = 0;
    m_timeMultiplier = 1.0f;
    m_transitionScript = 0;
    for (TODSystem& system : m_systems) {
        system.time = 0;
        system.stateLength = 60000;
        system.transitionLength = 30000;
        system.state = kAfternoon;
        system.frame = 0;
        system.daysPassed = 0;
        system.transitioning = false;
        system.scriptRun = false;
        system.params->Clear();
    }
    m_tempTOD = false;
    m_active = true;
    m_fogActive = true;
    m_levelLightsReset = false;
    m_unknown4->Clear();
    m_deadObjects->FlushAllItems();
}

// 0x00573180
void AddBloomParams(int on, int bloom1, int bloom2, uint32_t strength, uint32_t color, Script::CStruct* out)
{
    out->AddInteger(kOn, on);
    out->AddInteger(kBloom1, bloom1);
    out->AddInteger(kBloom2, bloom2);
    Script::CArray* memory = Script::NewArray();
    Script::CArray* array = memory ? memory->Init() : nullptr;
    array->SetSizeAndType(3, Script::ESYMBOLTYPE_INTEGER);
    array->SetElement(0, strength & 0xFF);
    array->SetElement(1, strength >> 8 & 0xFF);
    array->SetElement(2, strength >> 16 & 0xFF);
    out->AddArray(kStrength, array);
    array->SetElement(0, color & 0xFF);
    array->SetElement(1, color >> 8 & 0xFF);
    array->SetElement(2, color >> 16 & 0xFF);
    out->AddArray(kColor, array);
    if (array) {
        array->Clear();
        Script::DeleteArray(array);
    }
}

// 0x005732D0
void CTODManager::SetSceneColor(Script::CStruct* params)
{
    uint32_t lightGroup = 0;
    params->GetChecksum(kLightGroup, &lightGroup, 0);
    uint32_t resetLightGroup = 0;
    params->GetChecksum(kResetLightGroup, &resetLightGroup, 0);
    bool sceneOnly = params->ContainsFlag(kSceneColorFlag);
    int value;
    params->GetInteger(kColor, &value, 1);
    // (the colour's fourth byte isn't set by the original - stack garbage; nor the sky colour's, the top byte of the
    // parameters' pointer whose argument slot it's kept in)
    uint32_t color = static_cast<uint32_t>(value) & 0xFFFFFF;
    uint32_t skyColor = 0;
    int sky = value;
    bool isSky = false;
    if (params->GetInteger(kSky, &sky, 0)) {
        isSky = true;
        skyColor = static_cast<uint32_t>(sky) & 0xFFFFFF;
    }
    int start = -1;
    int end = -1;
    params->GetInteger(kSceneColorStart, &start, 0);
    params->GetInteger(kSceneColorEnd, &end, 0);
    ApplyColor(color, skyColor, isSky, lightGroup, resetLightGroup, sceneOnly, start, end);
}

// 0x00573400
void CTODManager::MarkPermanentlyDead(Script::CStruct* params)
{
    uint32_t name;
    if (params->GetChecksum(kName, &name, 0)) {
        if (!m_deadObjects->GetItem(name, true))
            m_deadObjects->PutItem(name, this);
        return;
    }
    uint32_t prefix;
    if (!params->GetChecksumOrStringChecksum(kPrefix, &prefix, 0))
        return;
    params->ContainsFlag(kNoPrefixWarning); // (see CreateObjects)
    uint16_t count = 0;
    uint32_t* nodes = SkateScript::GetPrefixedNodes(prefix, &count);
    for (int i = 0; i < count; ++i) {
        if (!m_deadObjects->GetItem(nodes[i]))
            m_deadObjects->PutItem(nodes[i], this);
    }
}

// 0x00573500
void CTODManager::SetBloom(int on, int bloom1, int bloom2, uint32_t strength, uint32_t color)
{
    Script::CStruct* params = NewStructure();
    if (on)
        AddBloomParams(on, bloom1, bloom2, strength, color, params);
    else
        params->AddInteger(kOn, 0);
    SetBloomStructure(params);
    DeleteStructure(params);
}

// 0x00573620
CTODManager* CTODManager::Construct()
{
    m_unknown4 = NewStructure();
    void* table = Mem::New(sizeof(Lst::HashTable));
    m_deadObjects = table ? new (table) Lst::HashTable(8) : nullptr;
    for (TODSystem& system : m_systems)
        system.params = NewStructure();
    Reset();
    return this;
}

// 0x00573770
void CTODManager::Destruct()
{
    for (TODSystem& system : m_systems) {
        DeleteStructure(system.params);
        system.params = nullptr;
    }
    DeleteStructure(m_unknown4);
    m_deadObjects->FlushAllItems();
    if (m_deadObjects) {
        m_deadObjects->~HashTable();
        Mem::Manager::Get()->Free(m_deadObjects);
    }
}

// 0x00573810
void CTODManager::UpdateSystem(int index)
{
    TODSystem& system = SystemAt(this, index);
    int part = system.part;
    ++system.frame;
    if (part != 0 && system.frame <= system.waitFrames)
        return;

    // This part's share of the sectors.
    double step = Hundred() / static_cast<double>(system.parts);
    int start = Ftol(part * step);
    int end = Ftol((part + 1) * step);
    float t = 1.0f;
    if (system.transitionLength) {
        double fraction = Fraction(system.time, system.transitionLength);
        t = static_cast<float>(fraction);
        if (!(fraction <= One()))
            t = 1.0f;
    }
    TODParams params = Lerp(system.current, system.next, t);
    uint32_t levelColor = Rgba(params.levRed, params.levGreen, params.levBlue, 0x80);
    uint32_t skyColor = Rgba(params.skyRed, params.skyGreen, params.skyBlue, 0x80);
    bool sky = system.sky;
    ApplyColor(levelColor, skyColor, sky, system.lightGroup, system.resetLightGroup, part > 0, start, end);
    if (!sky && !m_levelLightsReset)
        ApplyTODParams(params);

    // The next part (or the next group, after the last part).
    system.frame = 0;
    if (++system.part >= system.parts) {
        system.part = 0;
        if (system.group < system.groupCount - 1)
            ++system.group;
        else
            system.group = 0;
        ReadGroup(index);
    }
    if (!sky && !m_tempBloom.on)
        SetBloom(params.bloomOn, params.bloom1, params.bloom2, params.bloomStrength, params.bloomColor);

    // The transition script: half-way through (evening's at once).
    float threshold = system.state == kEvening ? Zero() : Half();
    if (!system.scriptRun && t > threshold) {
        system.scriptRun = true;
        Script::CStruct* scriptParams = NewStructure();
        scriptParams->AddChecksum(kCurrentTod, system.state);
        scriptParams->AddChecksum(kNextTod, system.nextState);
        scriptParams->AddStructure(kTodStateParams, system.params);
        if (!sky) {
            if (m_transitionScript)
                scriptParams->AddChecksum(kTransitionScript, m_transitionScript);
            Script::SpawnScript(kStateTransition, scriptParams, 0, nullptr, 0, 0, false, false, false, false);
        }
        DeleteStructure(scriptParams);
    }

    if (system.time > system.transitionLength) {
        NextState(index);
        if (!system.sky && !m_tempBloom.on) {
            const TODParams& current = system.current;
            SetBloom(current.bloomOn, current.bloom1, current.bloom2, current.bloomStrength, current.bloomColor);
        }
    }
}

// 0x00573BE0
void CTODManager::ResetLevelLights(uint32_t stateName)
{
    if (IsMultiplayer())
        return;
    TODParams params;
    if (!stateName) {
        params = GetTODParams(-1);
        ApplyTODParams(params);
        m_levelLightsReset = false;
    } else {
        std::memset(&params, 0, sizeof(params)); // (the original's are uninitialised: what the state doesn't give)
        Script::CStruct* state;
        SystemAt(this, CurrentSystem()).params->GetStructure(stateName, &state, 1);
        ParseParams(state, &params);
        ApplyTODParams(params);
        m_levelLightsReset = true;
    }
    SetBloom(params.bloomOn, params.bloom1, params.bloom2, params.bloomStrength, params.bloomColor);
}

// 0x00573CE0
void CTODManager::RefreshAll(bool tempState)
{
    bool wasActive = m_active;
    m_active = true;
    for (TODSystem& system : m_systems) {
        const TODParams& current = system.current;
        uint32_t levelColor = Rgba(current.levRed, current.levGreen, current.levBlue, 0x80);
        uint32_t skyColor = Rgba(current.skyRed, current.skyGreen, current.skyBlue, 0x80);
        bool sky = system.sky;
        Script::CArray* groups;
        system.params->GetArray(kGroups, &groups, 1);
        for (int g = 0; g < system.groupCount; ++g) {
            auto* group = reinterpret_cast<Script::CStruct*>(groups->GetElement(static_cast<uint32_t>(g)));
            uint32_t lightGroup;
            group->GetChecksum(kLightGroup, &lightGroup, 1);
            uint32_t resetLightGroup = 0;
            group->GetChecksum(kResetLightGroup, &resetLightGroup, 0);
            float step = static_cast<float>(Hundred() / static_cast<double>(system.parts));
            for (int part = 0; part < system.parts; ++part) {
                int start = Ftol(static_cast<double>(part) * step);
                int end = Ftol(static_cast<double>(part + 1) * step);
                ApplyColor(levelColor, skyColor, sky, lightGroup, resetLightGroup, part > 0, start, end);
            }
            if (!sky && !m_levelLightsReset)
                ApplyTODParams(system.current);
        }
        if (sky)
            continue;
        Script::CStruct* params = NewStructure();
        params->AddChecksum(kCurrentTod, PreviousTODState(system.state));
        params->AddChecksum(kNextTod, system.state);
        params->AddStructure(kTodStateParams, system.params);
        if (m_transitionScript)
            params->AddChecksum(kTransitionScript, m_transitionScript);
        params->AddChecksum(0u, kInstantChange);
        if (tempState)
            params->AddChecksum(0u, kTempState);
        Script::CStruct* bloom = NewStructure();
        AddBloomParams(current.bloomOn, current.bloom1, current.bloom2, current.bloomStrength, current.bloomColor, bloom);
        params->AddStructure(kBloomParams, bloom);
        DeleteStructure(bloom);
        Script::SpawnScript(kStateTransition, params, 0, nullptr, 0, 0, false, false, false, false);
        DeleteStructure(params);
    }
    m_active = wasActive;
    const TODParams& current = SystemAt(this, CurrentSystem()).current;
    SetBloom(current.bloomOn, current.bloom1, current.bloom2, current.bloomStrength, current.bloomColor);
    UpdateSun();
}

// 0x005741B0
void CTODManager::SetTempTOD(Script::CStruct* params)
{
    m_tempTOD = true;
    Script::FindSymbol(kTempTODChanged)->integer = 1;
    for (TODSystem& system : m_systems)
        ParseParams(params, &system.current);
    RefreshAll(true);
}

// 0x00574210
void CTODManager::RestoreCurrentTOD()
{
    for (TODSystem& system : m_systems) {
        Script::CStruct* state;
        system.params->GetStructure(system.state, &state, 1);
        ParseParams(state, &system.current);
    }
    RefreshAll(false);
    m_tempTOD = false;
}

// 0x00574270
void CTODManager::SetTempBloomParams(Script::CStruct* params)
{
    params->GetInteger(kOn, &m_tempBloom.on, 0);
    params->GetInteger(kBloom1, &m_tempBloom.bloom1, 0);
    params->GetInteger(kBloom2, &m_tempBloom.bloom2, 0);
    Script::CArray* array = nullptr;
    if (params->GetArray(kStrength, &array, 0))
        m_tempBloom.strength = ColorFromArray(array);
    array = nullptr;
    if (params->GetArray(kColor, &array, 0))
        m_tempBloom.color = ColorFromArray(array);
    // Off: back to the state's bloom (in a single player game).
    if (!m_tempBloom.on && !IsMultiplayer()) {
        BloomParams bloom = BloomOf(SystemAt(this, CurrentSystem()).current);
        SetBloom(bloom.on, bloom.bloom1, bloom.bloom2, bloom.strength, bloom.color);
        return;
    }
    SetBloom(m_tempBloom.on, m_tempBloom.bloom1, m_tempBloom.bloom2, m_tempBloom.strength, m_tempBloom.color);
}

// 0x00574400
void CTODManager::EnableBloom(int on)
{
    if (IsMultiplayer())
        return;
    TODParams& current = SystemAt(this, CurrentSystem()).current;
    if (on == current.bloomOn)
        return;
    current.bloomOn = on;
    SetBloom(current.bloomOn, current.bloom1, current.bloom2, current.bloomStrength, current.bloomColor);
}

// 0x00574590
void CTODManager::Update()
{
    if (GameModeName() != kCareer || Script::GetInteger(kInactive) == 1 || m_tempTOD || !m_active || GamePaused())
        return;
    int elapsed = Ftol(Tmr::GetSlomoFrameSeconds() * m_timeMultiplier * Thousand());
    m_totalTime += elapsed;
    UpdateSun();
    for (int index = 0; index < kSystems; ++index) {
        TODSystem& system = m_systems[index];
        system.time += elapsed;
        if (!system.transitioning && system.time > system.stateLength) {
            system.time = 0;
            system.transitioning = true;
            ReadGroup(index);
        }
        if (system.transitioning)
            UpdateSystem(index);
    }
}

// 0x00574670 (a state that never comes - not one of the four - loops forever, as in the original)
void CTODManager::SetTODInstant(uint32_t state)
{
    if (IsMultiplayer())
        return;
    // How long until `state`, from the first non-sky system.
    TODSystem& current = SystemAt(this, CurrentSystem());
    int remaining = current.transitioning ? current.transitionLength - current.time
                                          : current.stateLength - current.time + current.transitionLength;
    for (uint32_t s = NextTODState(current.state); s != state; s = NextTODState(s)) {
        Script::CStruct* structure;
        current.params->GetStructure(s, &structure, 1);
        int length;
        structure->GetInteger(kStateLength, &length, 1);
        remaining += current.transitionLength + length;
    }
    // Every system moved on by that long.
    for (int index = 0; index < kSystems; ++index) {
        TODSystem& system = m_systems[index];
        int left = remaining;
        while (left > 0) {
            if (system.transitioning) {
                if (left < system.transitionLength - system.time) {
                    system.time += left;
                    break;
                }
                left += system.time - system.transitionLength;
                NextState(index);
                ReadGroup(index);
            } else {
                if (left < system.stateLength - system.time) {
                    system.time += left;
                    break;
                }
                left += system.time - system.stateLength;
                system.transitioning = true;
                system.time = 0;
            }
        }
    }
    RefreshAll(false);
}

CTODManager*& TODManager() { return GameVariable<CTODManager*>(0x007CE490); }
namespace {
uint32_t& References() { return GameVariable<uint32_t>(0x007CE494); }
} // namespace

// 0x00574540
CTODManager* CreateTODManager(bool create)
{
    CTODManager*& manager = TODManager();
    if (!manager && create) {
        void* memory = Mem::Calloc(sizeof(CTODManager), nullptr, 1);
        manager = memory ? static_cast<CTODManager*>(memory)->Construct() : nullptr;
    }
    ++References();
    return manager;
}

// 0x00574910
void ReleaseTODManager()
{
    if (References() > 1) {
        --References();
        return;
    }
    CTODManager* manager = TODManager();
    if (manager) {
        manager->Destruct();
        Mem::Manager::Get()->Free(manager);
    }
    TODManager() = nullptr;
    References() = 0;
}

// --- the script commands ------------------------------------------------------------------------------------

// 0x00571FE0: TODManager_SetActive <0|1>
bool __cdecl CFunc_TODManager_SetActive(Script::CStruct* params, Script::CScript* /*script*/)
{
    CTODManager* manager = TODManager();
    int active;
    params->GetInteger(0u, &active, 1);
    manager->m_active = active != 0;
    return true;
}

// 0x00572010: TODManager_GetNumberOfDaysPassed [system_index=n] -> num_days_passed
bool __cdecl CFunc_TODManager_GetNumberOfDaysPassed(Script::CStruct* params, Script::CScript* script)
{
    CTODManager* manager = TODManager();
    int index = 0;
    params->GetInteger(kSystemIndex, &index, 0);
    script->m_params->AddInteger(kNumDaysPassed, SystemAt(manager, index).daysPassed);
    return true;
}

// 0x00572060: TODManager_SetTimeMultiplier <multiplier> (default 1)
bool __cdecl CFunc_TODManager_SetTimeMultiplier(Script::CStruct* params, Script::CScript* /*script*/)
{
    float multiplier = 1.0f;
    params->GetFloat(0u, &multiplier, 0);
    if (!TODManager())
        return false;
    TODManager()->m_timeMultiplier = multiplier;
    return true;
}

// 0x005720A0: TODManager_GetTimeMultiplier -> TOD_Time_Multiplier
bool __cdecl CFunc_TODManager_GetTimeMultiplier(Script::CStruct* /*params*/, Script::CScript* script)
{
    if (!TODManager())
        return false;
    script->m_params->AddFloat(kTimeMultiplierName, TODManager()->m_timeMultiplier);
    return true;
}

// 0x005720D0: TODManager_SetTODScript <script>
bool __cdecl CFunc_TODManager_SetTODScript(Script::CStruct* params, Script::CScript* /*script*/)
{
    CTODManager* manager = TODManager();
    uint32_t name = 0;
    params->GetChecksum(0u, &name, 0);
    manager->m_transitionScript = name;
    return true;
}

// 0x00572110: TODManager_Create name= | prefix=
bool __cdecl CFunc_TODManager_Create(Script::CStruct* params, Script::CScript* script)
{
    TODManager()->CreateObjects(params, script);
    return true;
}

// 0x00572BE0: TODManager_SetParams
bool __cdecl CFunc_TODManager_SetParams(Script::CStruct* params, Script::CScript* /*script*/)
{
    TODManager()->SetParams(params);
    return true;
}

// 0x00572C00: TODManager_GetCurrentTODParams [system_index=n] -> tod_params={active current_state_name lev_red ...}
bool __cdecl CFunc_TODManager_GetCurrentTODParams(Script::CStruct* params, Script::CScript* script)
{
    int index = -1;
    params->GetInteger(kSystemIndex, &index, 0);
    CTODManager* manager = TODManager();
    TODParams p = manager->GetTODParams(index);
    Script::CStruct* out = NewStructure();
    out->AddInteger(kActive, manager->m_active);
    int current = 0;
    while (current < CTODManager::kSystems && manager->m_systems[current].sky)
        ++current;
    out->AddChecksum(kCurrentStateName, SystemAt(manager, current).state);
    out->AddInteger(kLevRed, p.levRed);
    out->AddInteger(kLevGreen, p.levGreen);
    out->AddInteger(kLevBlue, p.levBlue);
    out->AddInteger(kSkyRed, p.skyRed);
    out->AddInteger(kSkyGreen, p.skyGreen);
    out->AddInteger(kSkyBlue, p.skyBlue);
    out->AddInteger(kFogOn, p.fogOn);
    out->AddInteger(kFogRed, p.fogRed);
    out->AddInteger(kFogGreen, p.fogGreen);
    out->AddInteger(kFogBlue, p.fogBlue);
    out->AddInteger(kFogAlpha, p.fogAlpha);
    out->AddInteger(kFogDist, p.fogDist);
    out->AddInteger(kAmbientRed, p.ambientRed);
    out->AddInteger(kAmbientGreen, p.ambientGreen);
    out->AddInteger(kAmbientBlue, p.ambientBlue);
    out->AddFloat(kAmbientModLo, p.ambientModLo);
    out->AddFloat(kAmbientModHi, p.ambientModHi);
    out->AddFloat(kHeading0, p.heading0);
    out->AddFloat(kPitch0, p.pitch0);
    out->AddInteger(kRed0, p.red0);
    out->AddInteger(kGreen0, p.green0);
    out->AddInteger(kBlue0, p.blue0);
    out->AddFloat(kModLo0, p.modLo0);
    out->AddFloat(kModHi0, p.modHi0);
    out->AddFloat(kHeading1, p.heading1);
    out->AddFloat(kPitch1, p.pitch1);
    out->AddInteger(kRed1, p.red1);
    out->AddInteger(kGreen1, p.green1);
    out->AddInteger(kBlue1, p.blue1);
    out->AddFloat(kModLo1, p.modLo1);
    out->AddFloat(kModHi1, p.modHi1);
    out->AddInteger(kBloomOn, p.bloomOn);
    out->AddInteger(kBloom1, p.bloom1);
    out->AddInteger(kBloom2, p.bloom2);
    Script::CArray* memory = Script::NewArray();
    Script::CArray* array = memory ? memory->Init() : nullptr;
    array->SetSizeAndType(3, Script::ESYMBOLTYPE_INTEGER);
    array->SetElement(0, p.bloomStrength & 0xFF);
    array->SetElement(1, p.bloomStrength >> 8 & 0xFF);
    array->SetElement(2, p.bloomStrength >> 16 & 0xFF);
    out->AddArray(kBloomStrength, array);
    array->SetElement(0, p.bloomColor & 0xFF);
    array->SetElement(1, p.bloomColor >> 8 & 0xFF);
    array->SetElement(2, p.bloomColor >> 16 & 0xFF);
    out->AddArray(kBloomColor, array);
    Script::CleanUpArray(array);
    if (array) {
        array->Clear();
        Script::DeleteArray(array);
    }
    script->m_params->AddStructurePointer(kTodParams, out);
    return true;
}

// 0x005730A0: TODManager_SetFogActive [0|1] (default 1)
bool __cdecl CFunc_TODManager_SetFogActive(Script::CStruct* params, Script::CScript* /*script*/)
{
    CTODManager* manager = TODManager();
    int on = 1;
    params->GetInteger(0u, &on, 0);
    manager->SetFogActive(on != 0);
    return true;
}

// 0x005735F0: TODManager_MarkPermanentlyDead name= | prefix=
bool __cdecl CFunc_TODManager_MarkPermanentlyDead(Script::CStruct* params, Script::CScript* /*script*/)
{
    TODManager()->MarkPermanentlyDead(params);
    return true;
}

// 0x00573610: TODManager_Reset
bool __cdecl CFunc_TODManager_Reset(Script::CStruct* /*params*/, Script::CScript* /*script*/)
{
    TODManager()->Reset();
    return true;
}

// 0x00574480: TODManager_ResetLevelLights [state_name=]
bool __cdecl CFunc_TODManager_ResetLevelLights(Script::CStruct* params, Script::CScript* /*script*/)
{
    CTODManager* manager = TODManager();
    uint32_t name = 0;
    params->GetChecksum(kStateName, &name, 0);
    manager->ResetLevelLights(name);
    return true;
}

// 0x005744C0: TODManager_SetTempTOD <params>
bool __cdecl CFunc_TODManager_SetTempTOD(Script::CStruct* params, Script::CScript* /*script*/)
{
    TODManager()->SetTempTOD(params);
    return true;
}

// 0x005744E0: TODManager_RestoreCurrentTOD
bool __cdecl CFunc_TODManager_RestoreCurrentTOD(Script::CStruct* /*params*/, Script::CScript* /*script*/)
{
    TODManager()->RestoreCurrentTOD();
    return true;
}

// 0x005744F0: TODManager_SetTempBloomParams on= bloom1= bloom2= strength=[] color=[]
bool __cdecl CFunc_TODManager_SetTempBloomParams(Script::CStruct* params, Script::CScript* /*script*/)
{
    TODManager()->SetTempBloomParams(params);
    return true;
}

// 0x00574510: TODManager_EnableBloom <0|1>
bool __cdecl CFunc_TODManager_EnableBloom(Script::CStruct* params, Script::CScript* /*script*/)
{
    CTODManager* manager = TODManager();
    int on;
    params->GetInteger(0u, &on, 1);
    manager->EnableBloom(on);
    return true;
}

// 0x00574840: TODManager_SetTODInstant <state>
bool __cdecl CFunc_TODManager_SetTODInstant(Script::CStruct* params, Script::CScript* /*script*/)
{
    uint32_t state;
    params->GetChecksum(0u, &state, 1);
    if (!TODManager())
        return false;
    TODManager()->SetTODInstant(state);
    return true;
}

// 0x00574870: TODManager_AdvanceTODState: straight to the next state.
bool __cdecl CFunc_TODManager_AdvanceTODState(Script::CStruct* /*params*/, Script::CScript* /*script*/)
{
    CTODManager* manager = TODManager();
    if (!manager)
        return false;
    int current = 0;
    while (current < CTODManager::kSystems && manager->m_systems[current].sky)
        ++current;
    manager->SetTODInstant(NextTODState(SystemAt(manager, current).state));
    return true;
}

} // namespace Gfx

REPLACE(0x00571140, &Gfx::CTODManager::GetLevelColor);
REPLACE(0x00571290, &Gfx::CTODManager::ReadGroup);
REPLACE(0x00571340, &Gfx::CTODManager::Lerp);
REPLACE(0x00571A00, &Gfx::CTODManager::ParseParams);
REPLACE(0x00571E00, &Gfx::CTODManager::GetTODParams);
REPLACE(0x00571EF0, &Gfx::CTODManager::CreateObjects);
REPLACE(0x00571FE0, Gfx::CFunc_TODManager_SetActive);
REPLACE(0x00572010, Gfx::CFunc_TODManager_GetNumberOfDaysPassed);
REPLACE(0x00572060, Gfx::CFunc_TODManager_SetTimeMultiplier);
REPLACE(0x005720A0, Gfx::CFunc_TODManager_GetTimeMultiplier);
REPLACE(0x005720D0, Gfx::CFunc_TODManager_SetTODScript);
REPLACE(0x00572110, Gfx::CFunc_TODManager_Create);
REPLACE(0x00572130, &Gfx::CTODManager::SetParams);
REPLACE(0x00572320, &Gfx::CTODManager::SetFogActive);
REPLACE(0x005723C0, &Gfx::CTODManager::NextState);
REPLACE(0x005724D0, &Gfx::CTODManager::UpdateSun);
REPLACE(0x005725F0, Gfx::ApplyColorToScene);
REPLACE(0x005727F0, &Gfx::CTODManager::ApplyColor);
// 0x00572821 is ApplyColor's body after its first test (jumped to from inside it; no callers) that Ghidra made a
// function of: its source is ApplyColor's.
REPLACE_SOURCE_ONLY(0x00572821, &Gfx::CTODManager::ApplyColor);
REPLACE(0x00572980, Gfx::SetLightDirection);
REPLACE(0x00572A10, &Gfx::CTODManager::ApplyTODParams);
REPLACE(0x00572BD0, &Gfx::CTODManager::ClearDeadObjects);
REPLACE(0x00572BE0, Gfx::CFunc_TODManager_SetParams);
REPLACE(0x00572C00, Gfx::CFunc_TODManager_GetCurrentTODParams);
REPLACE(0x005730A0, Gfx::CFunc_TODManager_SetFogActive);
REPLACE(0x005730E0, &Gfx::CTODManager::Reset);
REPLACE(0x00573180, Gfx::AddBloomParams);
REPLACE(0x005732D0, &Gfx::CTODManager::SetSceneColor);
REPLACE(0x00573400, &Gfx::CTODManager::MarkPermanentlyDead);
REPLACE(0x00573500, &Gfx::CTODManager::SetBloom);
REPLACE(0x005735F0, Gfx::CFunc_TODManager_MarkPermanentlyDead);
REPLACE(0x00573610, Gfx::CFunc_TODManager_Reset);
REPLACE(0x00573620, &Gfx::CTODManager::Construct);
REPLACE(0x00573770, &Gfx::CTODManager::Destruct);
REPLACE(0x00573810, &Gfx::CTODManager::UpdateSystem);
REPLACE(0x00573BE0, &Gfx::CTODManager::ResetLevelLights);
REPLACE(0x00573CE0, &Gfx::CTODManager::RefreshAll);
REPLACE(0x005741B0, &Gfx::CTODManager::SetTempTOD);
REPLACE(0x00574210, &Gfx::CTODManager::RestoreCurrentTOD);
REPLACE(0x00574270, &Gfx::CTODManager::SetTempBloomParams);
REPLACE(0x00574400, &Gfx::CTODManager::EnableBloom);
REPLACE(0x00574480, Gfx::CFunc_TODManager_ResetLevelLights);
REPLACE(0x005744C0, Gfx::CFunc_TODManager_SetTempTOD);
REPLACE(0x005744E0, Gfx::CFunc_TODManager_RestoreCurrentTOD);
REPLACE(0x005744F0, Gfx::CFunc_TODManager_SetTempBloomParams);
REPLACE(0x00574510, Gfx::CFunc_TODManager_EnableBloom);
REPLACE(0x00574540, Gfx::CreateTODManager);
REPLACE(0x00574590, &Gfx::CTODManager::Update);
REPLACE(0x00574670, &Gfx::CTODManager::SetTODInstant);
REPLACE(0x00574840, Gfx::CFunc_TODManager_SetTODInstant);
REPLACE(0x00574870, Gfx::CFunc_TODManager_AdvanceTODState);
REPLACE(0x00574910, Gfx::ReleaseTODManager);
