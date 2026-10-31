// NOT ORIGINAL - THAW's BMX ridden in THUG2 (game/skater/thaw_bmx.h).

#include "game/skater/thaw_bmx.h"

#include <cstdio>
#include <string>
#include <unordered_map>

#include <SDL2/SDL.h>

#include "game/ass/resource_registry.h"
#include "game/core/checksums.h"
#include "game/core/mem.h"
#include "game/front/front_end.h"
#include "game/front/level_editor.h"
#include "game/gfx/screen.h"
#include "game/gfx/shadow_base.h"
#include "game/gfx/texture_cfuncs.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/param_struct.h"
#include "game/skater/rider.h"
#include "game/skater/thaw_bmx_parked.h"
#include "game/sys/platform_config.h"
#include "game/sys/mlp.h"
#include "game/sys/screenshot_files.h"
#include "game/sys/run_log.h"

namespace Skater::ThawBmx {

namespace {

uint32_t NameHash(const char* name) { return Checksums::HashFromText(name); }

template <typename T>
T& Prop(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}

// The core physics component's fields read here (THUG2's).
constexpr uint32_t kObject = 0xC;     // its skater
// The skater object's.
constexpr uint32_t kPosition = 0x4C;
constexpr uint32_t kFacing = 0x8C;

// The scripts and globals (thaw_bmx.ns, tools/pipeline/thaw_bmx/convert.ts).
constexpr const char* kPhysics = "thaw_bicycle_physics";
constexpr const char* kToggleScript = "thaw_bmx_toggle";
constexpr const char* kAfterResetScript = "thaw_bmx_after_reset";
constexpr const char* kSkeletonScript = "thaw_bmx_load_skeleton";
constexpr const char* kRiderScript = "animload_thaw_bmx_rider";
constexpr const char* kBikeScript = "animload_thaw_bmx_bike";
// What those load (an animation's asset is its descChecksum plus the reference checksum it's loaded under).
constexpr const char* kSkeleton = "THAW_SI_BMX";
constexpr const char* kRiderReference = "THPS6_human";
constexpr const char* kTestAnim = "bmx_idle";

// THAW's bike members (its core physics table, THAW 0x00732368) and ours.
const char* const kMembers[] = {
    "SkaterOnBike",   "SkaterOffBike", "IsSkaterOnBike",   "BikeIsInOverGrind", "GetBikeManualAngle",
    "ResetBikeManualAngle", "BikeInManual", "BikeInXup",   "BikeEnterXup",      "BikeExitXup",
    "BikeInTireTap",  "BikeEnterSkid", "BikeExitSkid",     "bikeinskid",        "BikeCanSkid",
    "BikeExitTireTap", "exitbikeendo", "EnterWheelieFromAir", "ThawBmxLoadAssets",
    // (phase 4: the other THAW-only members THAW's bike scripts ask - thaw_bmx_tricks.cpp CallTrickMember)
    "DisplayPitchGreaterThan", "LandedFromTireTap", "InTireTap", "PitchingForward", "PitchingBackward",
    "ShouldRunStallScript", "ClearStallScriptFlag", "ExitStallBoost", "OnStall",
    // (ours: bike_playturnanim's turn animation on / off - thaw_bmx_physics.cpp SteerAnim)
    "ThawBmxSteerAnim",
    // (THAW's CameraLookAround members - the d-pad turns the camera on the bike: camera_lookaround.cpp, answered only
    // while the feature is on)
    "LookaroundOnDPad_On", "LookaroundOnDPad_Off",
};

std::unordered_map<const void*, BikeState>& States()
{
    static std::unordered_map<const void*, BikeState> states;
    return states;
}

bool RunIfThere(const char* name, GameObjects::CEntity* object)
{
    uint32_t checksum = NameHash(name);
    if (!Scripts::ScriptExists(checksum)) {
        Log("thaw_bmx: the script %s isn't loaded - make the overlay with tools/pipeline/menu_level/make_menu_level.ts after "
            "tools/pipeline/thaw_bmx/convert.ts",
            name);
        return false;
    }
    Scripts::LaunchScript(checksum, nullptr, object, false, name);
    return true;
}

// The bike's skeleton and both animation sets, loaded once for good (permanent, on the bottom-up heap as the game's
// own start-up loads): LoadAnim skips one already there, LoadSkeleton wouldn't - each set only when missing.
bool LoadAssets()
{
    AssetStore::CResourceRegistry* assets = AssetStore::AssetManager();
    if (!assets)
        return false;
    const uint32_t skeleton = NameHash(kSkeleton);
    const uint32_t rider = NameHash(kTestAnim) + NameHash(kRiderReference);
    const uint32_t bike = NameHash(kTestAnim) + NameHash(kSkeleton);
    if (assets->AssetExists(skeleton) && assets->AssetExists(rider) && assets->AssetExists(bike))
        return true;
    bool permanent = assets->GetPermanentByDefault();
    assets->SetPermanentByDefault(true);
    MemSys::EnterHeapScope(MemSys::BottomUpHeap());
    bool ok = true;
    if (!assets->AssetExists(skeleton))
        ok &= RunIfThere(kSkeletonScript, nullptr);
    if (!assets->AssetExists(rider))
        ok &= RunIfThere(kRiderScript, nullptr);
    if (!assets->AssetExists(bike))
        ok &= RunIfThere(kBikeScript, nullptr);
    MemSys::LeaveHeapScope();
    assets->SetPermanentByDefault(permanent);
    ok = ok && assets->AssetExists(skeleton) && assets->AssetExists(rider) && assets->AssetExists(bike);
    Log("thaw_bmx: the bike's skeleton and animations %s", ok ? "loaded" : "NOT loaded (see above)");
    return ok;
}

// ThawBmx_Available: the feature on (and the overlay's scripts there) - the pause menu's RIDE BMX item.
bool __cdecl CFunc_Available(Scripts::CStruct*, Scripts::CScript*)
{
    return IsSwitchedOn() && Scripts::ScriptExists(NameHash(kToggleScript));
}

// ThawBmx_LoadTexture "<folder/name>": IngestSkin unless the texture is loaded already (THAW's bike manual meter
// script, create_bike_manual_meter, loads its converted pictures with it each time a manual starts - THAW has them
// loaded from start-up). True when the texture is there.
bool __cdecl CFunc_LoadTexture(Scripts::CStruct* params, Scripts::CScript* script)
{
    const char* file = nullptr;
    if (!params->FetchText(0u, &file, 0) || !file)
        return false;
    return ScriptCommands::TextureLoaded(file) || ScriptCommands::CFunc_LoadTexture(params, script);
}

int g_key = SDL_SCANCODE_F7;
bool g_keyUp = true;

} // namespace

bool& IsSwitchedOn()
{
    static bool on = ConfigInt("features", "thaw_bmx", 0) != 0;
    return on;
}

BikeState* StateOf(const void* core, bool create)
{
    auto& states = States();
    auto it = states.find(core);
    if (it != states.end())
        return &it->second;
    if (!create)
        return nullptr;
    return &states[core];
}

void Forget(const void* core) { States().erase(core); }

bool OnBike(const void* core)
{
    const BikeState* state = core ? StateOf(core, false) : nullptr;
    return state && state->on;
}

uint32_t BikeSkaterScript(uint32_t name, uint32_t callerName, bool bikeScriptLoaded)
{
    static const uint32_t kPedKnockDown = NameHash("PedKnockDown");
    static const uint32_t kBikeKnockDown = NameHash("bike_PedKnockDown");
    static const uint32_t kKnockDownState = NameHash("STATECONTROL_knockdown");
    if (name != kPedKnockDown)
        return name;
    if (callerName == kKnockDownState)
        return 0;
    return bikeScriptLoaded ? kBikeKnockDown : name;
}

uint32_t SkaterScript(const GameObjects::CRider* skater, uint32_t name, const Scripts::CScript* caller)
{
    if (!IsSwitchedOn() || !skater || !OnBike(skater->m_corePhysics))
        return name;
    static const uint32_t kBikeKnockDown = NameHash("bike_PedKnockDown");
    uint32_t chosen = BikeSkaterScript(name, caller ? caller->m_scriptTitle : 0, Scripts::ScriptExists(kBikeKnockDown));
    if (chosen == 0)
        Log("thaw_bmx: a ped's knock-down on the bike - not knocked over (THAW's STATESCRIPT_knockdown)");
    else if (chosen != name)
        Log("thaw_bmx: knocked over by a ped on the bike - bike_PedKnockDown");
    return chosen;
}

bool HalfStickKey()
{
    if (!IsSwitchedOn())
        return false;
    for (const auto& entry : States())
        if (entry.second.on)
            return true;
    return false;
}

Scripts::CStruct* PhysicsStructure(const void* core)
{
    static int s_logged = -1; // (what was logged last: -1 nothing, 0 the skater's, 1 the bike's, 2 the bike's missing)
    if (!OnBike(core)) {
        if (s_logged > 0) {
            Log("thaw_bmx: off the bike - the skater's physics again");
            s_logged = 0;
        }
        return nullptr;
    }
    Scripts::CStruct* physics = Scripts::ReadSubStruct(NameHash(kPhysics));
    int now = physics ? 1 : 2;
    if (now != s_logged) {
        Log(physics ? "thaw_bmx: on the bike - %s in use"
                    : "thaw_bmx: on the bike, but %s isn't loaded (the overlay's specialitems.qb) - the skater's physics",
            kPhysics);
        s_logged = now;
    }
    return physics;
}

void SetWheelPoints(BikeState& state, const Maths::Vec4& position, const Maths::Vec4& facing)
{
    // (THAW: the front one along the facing, the rear one back; w as the sum's)
    state.frontWheel = {position.x + facing.x * kWheelOffset, position.y + facing.y * kWheelOffset,
                        position.z + facing.z * kWheelOffset, position.w + facing.w * kWheelOffset};
    state.rearWheel = {position.x - facing.x * kWheelOffset, position.y - facing.y * kWheelOffset,
                       position.z - facing.z * kWheelOffset, position.w - facing.w * kWheelOffset};
}

int InvokeMethodByName(void* core, uint32_t name, Scripts::CStruct* params, Scripts::CScript* script)
{
    // (the names THAW's members have; ThawBmxLoadAssets is ours)
    static const uint32_t kOn = NameHash("SkaterOnBike"), kOff = NameHash("SkaterOffBike"),
                          kIsOn = NameHash("IsSkaterOnBike"), kOverGrind = NameHash("BikeIsInOverGrind"),
                          kGetAngle = NameHash("GetBikeManualAngle"), kResetAngle = NameHash("ResetBikeManualAngle"),
                          kInManual = NameHash("BikeInManual"), kInXup = NameHash("BikeInXup"),
                          kEnterXup = NameHash("BikeEnterXup"), kExitXup = NameHash("BikeExitXup"),
                          kInTireTap = NameHash("BikeInTireTap"), kEnterSkid = NameHash("BikeEnterSkid"),
                          kExitSkid = NameHash("BikeExitSkid"), kInSkid = NameHash("bikeinskid"),
                          kCanSkid = NameHash("BikeCanSkid"), kExitTireTap = NameHash("BikeExitTireTap"),
                          kExitEndo = NameHash("exitbikeendo"), kLoad = NameHash("ThawBmxLoadAssets"),
                          kEnterWheelie = NameHash("EnterWheelieFromAir"),
                          kManualAngle = NameHash("manual_angle"), kNose = NameHash("nose");
    auto nose = [&](bool set) {
        if (set && script && script->m_args)
            script->m_args->PutInt(kNose, 1);
    };
    if (name == kIsOn)
        return OnBike(core) ? 1 : 0;
    if (name == kLoad)
        return LoadAssets() ? 1 : 0;
    if (name == kEnterWheelie) // (THAW 0x005CA3B0: phase 3, thaw_bmx_physics.cpp)
        return EnterWheelieFromAir(core, params);
    static const uint32_t kSteerAnim = NameHash("ThawBmxSteerAnim"), kOffFlag = NameHash("off");
    if (name == kSteerAnim) { // (ours: `on` / `off`)
        SetSteerAnim(core, !(params && params->HasBareFlag(kOffFlag)));
        return 1;
    }
    // (the rest read or change the state; one is made on first use)
    bool known = name == kOn || name == kOff || name == kOverGrind || name == kGetAngle || name == kResetAngle ||
                 name == kInManual || name == kInXup || name == kEnterXup || name == kExitXup || name == kInTireTap ||
                 name == kEnterSkid || name == kExitSkid || name == kInSkid || name == kCanSkid ||
                 name == kExitTireTap || name == kExitEndo;
    if (!known)
        return CallTrickMember(core, name, params, script); // (phase 4's: thaw_bmx_tricks.cpp)
    BikeState& state = *StateOf(core, true);
    if (name == kOn) { // 0x005C98E0
        state.on = true;
        void* skater = Prop<void*>(core, kObject);
        SetWheelPoints(state, Prop<Maths::Vec4>(skater, kPosition), Prop<Maths::Vec4>(skater, kFacing));
        return 1;
    }
    if (name == kOff) { // 0x005C74C0 (THAW clears only the flag; ours the bike's other states too)
        SteerAnimOff(core);
        FlipAnimsToFlag(core);
        BikeState fresh;
        fresh.frontWheel = state.frontWheel;
        fresh.rearWheel = state.rearWheel;
        state = fresh;
        return 1;
    }
    if (name == kOverGrind) // 0x005C74E0
        return state.overGrind ? 1 : 0;
    if (name == kGetAngle) { // 0x005C74F0: manual_angle into the script's parameters
        if (script && script->m_args)
            script->m_args->PushFloat(kManualAngle, state.manualAngle);
        return 1;
    }
    if (name == kResetAngle) { // 0x005C7510
        state.manualAngle = kManualAngleReset;
        return 1;
    }
    if (name == kInManual) { // 0x005C7530: `nose` for a nose manual
        nose(state.noseManual);
        return state.manual ? 1 : 0;
    }
    if (name == kInXup) // 0x005C7570
        return state.xup ? 1 : 0;
    if (name == kEnterXup) { // 0x005C7580 (THAW returns what was in AL; true here)
        state.xup = true;
        return 1;
    }
    if (name == kExitXup) { // 0x005C7590
        state.xup = false;
        return 1;
    }
    if (name == kInTireTap) { // 0x005C75F0
        nose(state.tireTapNose);
        return state.tireTap ? 1 : 0;
    }
    if (name == kEnterSkid) { // 0x005C75C0 (as BikeEnterXup)
        state.skid = true;
        return 1;
    }
    if (name == kExitSkid) { // 0x005C75D0
        state.skid = false;
        state.canSkid = false;
        state.skidTimer = kSkidTimerReset;
        return 1;
    }
    if (name == kInSkid) // 0x005C75A0
        return state.skid ? 1 : 0;
    if (name == kCanSkid) // 0x005C75B0
        return state.canSkid ? 1 : 0;
    if (name == kExitTireTap) { // 0x005C7630 (THAW also stamps its state component's tire tap time)
        state.tireTap = false;
        state.tireTapNose = false;
        state.tapLedge = false; // (THAW: +0x54D)
        return 1;
    }
    // exitbikeendo, 0x005C8260
    state.endo = false;
    return 1;
}

void RegisterScriptFunctions()
{
    static const Scripts::CFunctionEntry functions[] = {
        {"ThawBmx_Available", reinterpret_cast<void*>(&CFunc_Available)},
        {"ThawBmx_LoadTexture", reinterpret_cast<void*>(&CFunc_LoadTexture)},
    };
    Scripts::InstallScriptFunctions(functions, sizeof(functions) / sizeof(functions[0]));
    if (!IsSwitchedOn())
        return;
    Scripts::PublishMethodTable(kMembers, sizeof(kMembers) / sizeof(kMembers[0]));
    RegisterParkedFunctions(); // (the parked bikes: thaw_bmx_parked.h)
    Engine3D::SetShadowCompanion(&ShadowCompanion);
    char setting[64] = "";
    BuildInfo::FetchText(BuildInfo::kKeybinds, "ThawBmx", "", setting, sizeof(setting));
    auto byName = [](const std::string& text) { return static_cast<int>(SDL_GetScancodeFromName(text.c_str())); };
    g_key = SystemCore::Screenshot::ParseKey(setting, SDL_SCANCODE_F7, byName);
    Log("thaw_bmx: on - %s gets on / off THAW's BMX in a level (also the pause menu, or the script %s)",
        SDL_GetScancodeName(static_cast<SDL_Scancode>(g_key)), kToggleScript);
}

void OnSkaterReset(GameObjects::CRider* skater)
{
    BikeState* state = StateOf(skater->m_corePhysics, false);
    if (!state || !state->on)
        return;
    // The bike (a special item) is gone: off it.
    Log("thaw_bmx: skater reset on the bike - off it");
    SteerAnimOff(skater->m_corePhysics);
    FlipAnimsToFlag(skater->m_corePhysics);
    *state = BikeState{};
    RunIfThere(kAfterResetScript, skater);
}

void TickFrame(GameModules::FrontEnd* frontEnd)
{
    if (!IsSwitchedOn())
        return;
    UpdateParked(frontEnd); // (the level's bike: thaw_bmx_parked.h)
    // Left for the front end (no level) with the bike's d-pad camera on - no bike script turned it off: the menus get
    // the stick back (bike_start's SetAnalogStickActiveForMenus 0 undone).
    if (DPadCameraNoted() && Engine3D::CurrentLevel() == 0 && frontEnd) {
        frontEnd->SetAnalogStickActiveForMenus(true);
        NoteDPadCamera(false);
        Log("thaw_bmx: no level with the bike's d-pad camera on - the stick moves through the menus again");
    }
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    bool down = keys && g_key > 0 && g_key < SDL_NUM_SCANCODES && keys[g_key];
    bool pressed = down && g_keyUp;
    g_keyUp = !down;
    if (!pressed || FrameLoop::FramesRun() == 0 || MenuSystem::LevelEditor::LiveNow() || (frontEnd && frontEnd->m_isHalted) ||
        Engine3D::CurrentLevel() == 0)
        return;
    RunIfThere(kToggleScript, nullptr);
}

} // namespace Skater::ThawBmx
