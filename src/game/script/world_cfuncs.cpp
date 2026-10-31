// Script commands (CFunctions) 0x005A5360-0x005A595F: light groups, the skaters' pause, the viewer, the weather
// and the screen effects (see world_cfuncs.h).

#include "game/script/world_cfuncs.h"

#include <cstdint>

#include "game/core/mem.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "game/skate/skate_module.h"
#include "game/sys/module.h"
#include "game/sys/viewer.h"
#include "game/gfx/weather.h"
#include "game/gfx/screen_effects.h"
#include "game/script/game_cfuncs.h"
#include "game/gfx/engine_glue.h"
#include "game/sys/game_config.h"

namespace CFuncs {

Mdl::Manager* g_moduleManager;
bool g_viewerLaunched;

namespace {

using Script::CScript;
using Script::CStruct;

constexpr uint32_t kViewerCam = 0xEB17151B; // "viewer_cam": the viewer camera object's name (its id)

// 0x005A4EF0 (no source yet): a command (7 light group, 5 alive...) applied to the node name= or the nodes
// prefix= names; true unless neither is given.
bool NodeCommand(CStruct* params, CScript* script, int command)
{
    return CFuncs::NodeCommandByName(params, script, command);
}

// The skater's own pause and unpause (its vtable +0x2C / +0x30).
void PauseSkater(Obj::CCompositeObject* skater)
{
    reinterpret_cast<void(__thiscall*)(Obj::CCompositeObject*)>(skater->m_vtable[11])(skater);
}
void UnpauseSkater(Obj::CCompositeObject* skater)
{
    reinterpret_cast<void(__thiscall*)(Obj::CCompositeObject*)>(skater->m_vtable[12])(skater);
}

// Whether LaunchViewer has made the viewer (0x007CEBF0).
bool& ViewerLaunched() { return g_viewerLaunched; }

// The weather (0x006FA7C8) and the screen effects (0x006FA7CC).
void* Weather() { return Nx::g_weather; }
void* ScreenEffects() { return Nx::g_screenEffects; }
using Setter = bool(__thiscall*)(void*, CStruct*, CScript*);

} // namespace

// 0x005A5360
bool __cdecl CFunc_SetLightGroup(CStruct* params, CScript* script) { return NodeCommand(params, script, 7); }

// 0x005A5380
bool __cdecl CFunc_IsAlive(CStruct* params, CScript* script) { return NodeCommand(params, script, 5); }

// 0x005A53A0
bool __cdecl CFunc_PauseSkaters(CStruct* params, CScript*)
{
    bool hide = params->ContainsFlag("hide");
    Mdl::Skate* skate = Mdl::Skate::Instance();
    uint32_t count = static_cast<uint32_t>(skate->GetNumSkaters());
    for (uint32_t i = 0; i < count; ++i) {
        Obj::CCompositeObject* skater = skate->GetSkater(static_cast<int>(i));
        if (skater && skater->m_unknownBC) {
            PauseSkater(skater);
            skater->Hide(hide);
        }
    }
    return true;
}

// 0x005A5420
bool __cdecl CFunc_UnPauseSkaters(CStruct*, CScript*)
{
    Mdl::Skate* skate = Mdl::Skate::Instance();
    uint32_t count = static_cast<uint32_t>(skate->GetNumSkaters());
    for (uint32_t i = 0; i < count; ++i) {
        Obj::CCompositeObject* skater = skate->GetSkater(static_cast<int>(i));
        if (skater) {
            UnpauseSkater(skater);
            skater->Hide(false);
        }
    }
    return true;
}

// 0x005A5480
bool __cdecl CFunc_SkatersAreReady(CStruct*, CScript*)
{
    if (!GameNet::g_gameNetManager->m_readyToPlay)
        return false;
    if (Mdl::Skate::Instance()->GetNumSkaters() > 0)
        return true;
    // IsObserving (0x005A0D90, no source yet)
    return CFuncs::CFunc_IsObserving(nullptr, nullptr);
}

// 0x005A54E0
bool __cdecl CFunc_LaunchViewer(CStruct*, CScript*)
{
    if (ViewerLaunched())
        return true;
    Mdl::Manager* modules = g_moduleManager;
    void* memory = Mem::Calloc(sizeof(Mdl::Viewer));
    Mdl::Viewer* viewer = memory ? static_cast<Mdl::Viewer*>(memory)->Construct() : nullptr;
    modules->RegisterModule(viewer);
    modules->StartModule(viewer);
    ViewerLaunched() = true;

    CStruct* nodeData = Script::NewStruct();
    if (nodeData)
        nodeData = nodeData->Init();
    nodeData->AddChecksum("name", kViewerCam);
    Obj::CCompositeObject* camera = Obj::CompositeObjectManager()->CreateCompositeObjectFromNode(
        Script::GetArray("viewercam_composite_structure"), nodeData, true);
    if (nodeData) {
        nodeData->Clear();
        Script::DeleteStruct(nodeData);
    }
    camera->m_objectFlags |= 2;
    return true;
}

// 0x005A5610
bool __cdecl CFunc_AddSkaterEarly(CStruct*, CScript*)
{
    if (Mdl::Skate::Instance()->GetNumSkaters() == 0) {
        Mdl::Skate* skate = Mdl::Skate::Instance();
        // its profile 0 (0x00564FC0) as a new local skater (0x00567180), no id, heap 0
        Obj::CSkaterProfile* profile = skate->GetProfile(static_cast<int>(0));
        skate->AddSkater(profile, true, 0, 0);
    }
    return true;
}

// --- the weather ---

// 0x005A5680: its vtable +0x04 (through 0x004A1090).
bool __cdecl CFunc_WeatherUpdateGrid(CStruct*, CScript*)
{
    reinterpret_cast<void(__thiscall*)(void*)>((*static_cast<void***>(Weather()))[1])(Weather()); // (0x004A1090: a jump through the vtable +0x04)
    return true;
}
// 0x005A5690
bool __cdecl CFunc_WeatherSetRainHeight(CStruct* params, CScript* script)
{
    return static_cast<Nx::CWeather*>(Weather())->ScriptSetRainHeight(params, script);
}
// 0x005A56B0
bool __cdecl CFunc_WeatherSetRainFrames(CStruct* params, CScript* script)
{
    return static_cast<Nx::CWeather*>(Weather())->ScriptSetRainFrames(params, script);
}
// 0x005A56D0
bool __cdecl CFunc_WeatherSetRainLength(CStruct* params, CScript* script)
{
    return static_cast<Nx::CWeather*>(Weather())->ScriptSetRainLength(params, script);
}
// 0x005A56F0
bool __cdecl CFunc_WeatherSetRainBlendMode(CStruct* params, CScript* script)
{
    return static_cast<Nx::CWeather*>(Weather())->ScriptSetRainBlendMode(params, script);
}
// 0x005A5710
bool __cdecl CFunc_WeatherSetRainRate(CStruct* params, CScript* script)
{
    return static_cast<Nx::CWeather*>(Weather())->ScriptSetRainRate(params, script);
}
// 0x005A5730
bool __cdecl CFunc_WeatherSetRainColor(CStruct* params, CScript* script)
{
    return static_cast<Nx::CWeather*>(Weather())->ScriptSetRainColor(params, script);
}
// 0x005A5750
bool __cdecl CFunc_WeatherSetSplashRate(CStruct* params, CScript* script)
{
    return static_cast<Nx::CWeather*>(Weather())->ScriptSetSplashRate(params, script);
}
// 0x005A5770
bool __cdecl CFunc_WeatherSetSplashLife(CStruct* params, CScript* script)
{
    return static_cast<Nx::CWeather*>(Weather())->ScriptSetSplashLife(params, script);
}
// 0x005A5790
bool __cdecl CFunc_WeatherSetSplashSize(CStruct* params, CScript* script)
{
    return static_cast<Nx::CWeather*>(Weather())->ScriptSetSplashSize(params, script);
}
// 0x005A57B0
bool __cdecl CFunc_WeatherSetSplashColor(CStruct* params, CScript* script)
{
    return static_cast<Nx::CWeather*>(Weather())->ScriptSetSplashColor(params, script);
}
// 0x005A57D0
bool __cdecl CFunc_WeatherSetSplashBlendMode(CStruct* params, CScript* script)
{
    return static_cast<Nx::CWeather*>(Weather())->ScriptSetSplashBlendMode(params, script);
}
// 0x005A57F0
bool __cdecl CFunc_WeatherSetSnowHeight(CStruct* params, CScript* script)
{
    return static_cast<Nx::CWeather*>(Weather())->ScriptSetSnowHeight(params, script);
}
// 0x005A5810
bool __cdecl CFunc_WeatherSetSnowFrames(CStruct* params, CScript* script)
{
    return static_cast<Nx::CWeather*>(Weather())->ScriptSetSnowFrames(params, script);
}
// 0x005A5830
bool __cdecl CFunc_WeatherSetSnowSize(CStruct* params, CScript* script)
{
    return static_cast<Nx::CWeather*>(Weather())->ScriptSetSnowSize(params, script);
}
// 0x005A5850
bool __cdecl CFunc_WeatherSetSnowBlendMode(CStruct* params, CScript* script)
{
    return static_cast<Nx::CWeather*>(Weather())->ScriptSetSnowBlendMode(params, script);
}
// 0x005A5870
bool __cdecl CFunc_WeatherSetSnowRate(CStruct* params, CScript* script)
{
    return static_cast<Nx::CWeather*>(Weather())->ScriptSetSnowRate(params, script);
}
// 0x005A5890
bool __cdecl CFunc_WeatherSetSnowColor(CStruct* params, CScript* script)
{
    return static_cast<Nx::CWeather*>(Weather())->ScriptSetSnowColor(params, script);
}
// 0x005A58B0
bool __cdecl CFunc_WeatherSetSnowActive(CStruct* params, CScript* script)
{
    return static_cast<Nx::CWeather*>(Weather())->ScriptSetSnowActive(params, script);
}
// 0x005A58D0
bool __cdecl CFunc_WeatherSetRainActive(CStruct* params, CScript* script)
{
    return static_cast<Nx::CWeather*>(Weather())->ScriptSetRainActive(params, script);
}

// --- the screen effects ---

// 0x005A58F0
bool __cdecl CFunc_SetScreenEffectsOn(CStruct* params, CScript* script)
{
    return static_cast<Nx::CScreenEffects*>(ScreenEffects())->SetOn(params, script);
}
// 0x005A5910
bool __cdecl CFunc_SetScreenEffectsOff(CStruct* params, CScript* script)
{
    return static_cast<Nx::CScreenEffects*>(ScreenEffects())->SetOff(params, script);
}
// 0x005A5930
bool __cdecl CFunc_SetScreenEffectsDOFParams(CStruct* params, CScript* script)
{
    return static_cast<Nx::CScreenEffects*>(ScreenEffects())->SetDOFParams(params, script);
}

// 0x005A5950
bool __cdecl CFunc_IsPal(CStruct*, CScript*) { return GameConfig::g_language != 2; }

} // namespace CFuncs
