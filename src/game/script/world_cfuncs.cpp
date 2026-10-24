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
#include "hook/game.h"
#include "hook/hook.h"

namespace CFuncs {

namespace {

using Script::CScript;
using Script::CStruct;

constexpr uint32_t kViewerCam = 0xEB17151B; // the viewer camera object's name (its id)

// 0x005A4EF0 (no source yet): a command (7 light group, 5 alive...) applied to the node name= or the nodes
// prefix= names; true unless neither is given.
bool NodeCommand(CStruct* params, CScript* script, int command)
{
    return GameFunction<bool(__cdecl*)(CStruct*, CScript*, int)>(0x005A4EF0)(params, script, command);
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
bool& ViewerLaunched() { return GameVariable<bool>(0x007CEBF0); }

// The weather (0x006FA7C8) and the screen effects (0x006FA7CC).
void* Weather() { return GameVariable<void*>(0x006FA7C8); }
void* ScreenEffects() { return GameVariable<void*>(0x006FA7CC); }
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
    if (!GameVariable<GameNet::Manager*>(0x007CCE14)->m_readyToPlay)
        return false;
    if (Mdl::Skate::Instance()->GetNumSkaters() > 0)
        return true;
    // IsObserving (0x005A0D90, no source yet)
    return GameFunction<bool(__cdecl*)(CStruct*, CScript*)>(0x005A0D90)(nullptr, nullptr);
}

// 0x005A54E0
bool __cdecl CFunc_LaunchViewer(CStruct*, CScript*)
{
    if (ViewerLaunched())
        return true;
    Mdl::Manager* modules = GameVariable<Mdl::Manager*>(0x00696D78);
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
        void* profile = GameFunction<void*(__thiscall*)(Mdl::Skate*, int)>(0x00564FC0)(skate, 0);
        GameFunction<void*(__thiscall*)(Mdl::Skate*, void*, bool, int, int)>(0x00567180)(skate, profile, true, 0, 0);
    }
    return true;
}

// --- the weather ---

// 0x005A5680: its vtable +0x04 (through 0x004A1090).
bool __cdecl CFunc_WeatherUpdateGrid(CStruct*, CScript*)
{
    GameFunction<void(__thiscall*)(void*)>(0x004A1090)(Weather());
    return true;
}
// 0x005A5690
bool __cdecl CFunc_WeatherSetRainHeight(CStruct* params, CScript* script)
{
    return GameFunction<Setter>(0x004A10B0)(Weather(), params, script);
}
// 0x005A56B0
bool __cdecl CFunc_WeatherSetRainFrames(CStruct* params, CScript* script)
{
    return GameFunction<Setter>(0x004A10D0)(Weather(), params, script);
}
// 0x005A56D0
bool __cdecl CFunc_WeatherSetRainLength(CStruct* params, CScript* script)
{
    return GameFunction<Setter>(0x004A1110)(Weather(), params, script);
}
// 0x005A56F0
bool __cdecl CFunc_WeatherSetRainBlendMode(CStruct* params, CScript* script)
{
    return GameFunction<Setter>(0x004A1130)(Weather(), params, script);
}
// 0x005A5710
bool __cdecl CFunc_WeatherSetRainRate(CStruct* params, CScript* script)
{
    return GameFunction<Setter>(0x004A1190)(Weather(), params, script);
}
// 0x005A5730
bool __cdecl CFunc_WeatherSetRainColor(CStruct* params, CScript* script)
{
    return GameFunction<Setter>(0x004A11B0)(Weather(), params, script);
}
// 0x005A5750
bool __cdecl CFunc_WeatherSetSplashRate(CStruct* params, CScript* script)
{
    return GameFunction<Setter>(0x004A1240)(Weather(), params, script);
}
// 0x005A5770
bool __cdecl CFunc_WeatherSetSplashLife(CStruct* params, CScript* script)
{
    return GameFunction<Setter>(0x004A1260)(Weather(), params, script);
}
// 0x005A5790
bool __cdecl CFunc_WeatherSetSplashSize(CStruct* params, CScript* script)
{
    return GameFunction<Setter>(0x004A1280)(Weather(), params, script);
}
// 0x005A57B0
bool __cdecl CFunc_WeatherSetSplashColor(CStruct* params, CScript* script)
{
    return GameFunction<Setter>(0x004A12A0)(Weather(), params, script);
}
// 0x005A57D0
bool __cdecl CFunc_WeatherSetSplashBlendMode(CStruct* params, CScript* script)
{
    return GameFunction<Setter>(0x004A12F0)(Weather(), params, script);
}
// 0x005A57F0
bool __cdecl CFunc_WeatherSetSnowHeight(CStruct* params, CScript* script)
{
    return GameFunction<Setter>(0x004A1350)(Weather(), params, script);
}
// 0x005A5810
bool __cdecl CFunc_WeatherSetSnowFrames(CStruct* params, CScript* script)
{
    return GameFunction<Setter>(0x004A1370)(Weather(), params, script);
}
// 0x005A5830
bool __cdecl CFunc_WeatherSetSnowSize(CStruct* params, CScript* script)
{
    return GameFunction<Setter>(0x004A13B0)(Weather(), params, script);
}
// 0x005A5850
bool __cdecl CFunc_WeatherSetSnowBlendMode(CStruct* params, CScript* script)
{
    return GameFunction<Setter>(0x004A13D0)(Weather(), params, script);
}
// 0x005A5870
bool __cdecl CFunc_WeatherSetSnowRate(CStruct* params, CScript* script)
{
    return GameFunction<Setter>(0x004A1430)(Weather(), params, script);
}
// 0x005A5890
bool __cdecl CFunc_WeatherSetSnowColor(CStruct* params, CScript* script)
{
    return GameFunction<Setter>(0x004A1450)(Weather(), params, script);
}
// 0x005A58B0
bool __cdecl CFunc_WeatherSetSnowActive(CStruct* params, CScript* script)
{
    return GameFunction<Setter>(0x004A14A0)(Weather(), params, script);
}
// 0x005A58D0
bool __cdecl CFunc_WeatherSetRainActive(CStruct* params, CScript* script)
{
    return GameFunction<Setter>(0x004A14B0)(Weather(), params, script);
}

// --- the screen effects ---

// 0x005A58F0
bool __cdecl CFunc_SetScreenEffectsOn(CStruct* params, CScript* script)
{
    return GameFunction<Setter>(0x0049E320)(ScreenEffects(), params, script);
}
// 0x005A5910
bool __cdecl CFunc_SetScreenEffectsOff(CStruct* params, CScript* script)
{
    return GameFunction<Setter>(0x0049E310)(ScreenEffects(), params, script);
}
// 0x005A5930
bool __cdecl CFunc_SetScreenEffectsDOFParams(CStruct* params, CScript* script)
{
    return GameFunction<Setter>(0x0049E4B0)(ScreenEffects(), params, script);
}

// 0x005A5950
bool __cdecl CFunc_IsPal(CStruct*, CScript*) { return GameVariable<int>(0x007CEFA0) != 2; }

} // namespace CFuncs

REPLACE(0x005A5360, CFuncs::CFunc_SetLightGroup);
REPLACE(0x005A5380, CFuncs::CFunc_IsAlive);
REPLACE(0x005A53A0, CFuncs::CFunc_PauseSkaters);
REPLACE(0x005A5420, CFuncs::CFunc_UnPauseSkaters);
REPLACE(0x005A5480, CFuncs::CFunc_SkatersAreReady);
REPLACE(0x005A54E0, CFuncs::CFunc_LaunchViewer);
REPLACE(0x005A5610, CFuncs::CFunc_AddSkaterEarly);
REPLACE(0x005A5680, CFuncs::CFunc_WeatherUpdateGrid);
REPLACE(0x005A5690, CFuncs::CFunc_WeatherSetRainHeight);
REPLACE(0x005A56B0, CFuncs::CFunc_WeatherSetRainFrames);
REPLACE(0x005A56D0, CFuncs::CFunc_WeatherSetRainLength);
REPLACE(0x005A56F0, CFuncs::CFunc_WeatherSetRainBlendMode);
REPLACE(0x005A5710, CFuncs::CFunc_WeatherSetRainRate);
REPLACE(0x005A5730, CFuncs::CFunc_WeatherSetRainColor);
REPLACE(0x005A5750, CFuncs::CFunc_WeatherSetSplashRate);
REPLACE(0x005A5770, CFuncs::CFunc_WeatherSetSplashLife);
REPLACE(0x005A5790, CFuncs::CFunc_WeatherSetSplashSize);
REPLACE(0x005A57B0, CFuncs::CFunc_WeatherSetSplashColor);
REPLACE(0x005A57D0, CFuncs::CFunc_WeatherSetSplashBlendMode);
REPLACE(0x005A57F0, CFuncs::CFunc_WeatherSetSnowHeight);
REPLACE(0x005A5810, CFuncs::CFunc_WeatherSetSnowFrames);
REPLACE(0x005A5830, CFuncs::CFunc_WeatherSetSnowSize);
REPLACE(0x005A5850, CFuncs::CFunc_WeatherSetSnowBlendMode);
REPLACE(0x005A5870, CFuncs::CFunc_WeatherSetSnowRate);
REPLACE(0x005A5890, CFuncs::CFunc_WeatherSetSnowColor);
REPLACE(0x005A58B0, CFuncs::CFunc_WeatherSetSnowActive);
REPLACE(0x005A58D0, CFuncs::CFunc_WeatherSetRainActive);
REPLACE(0x005A58F0, CFuncs::CFunc_SetScreenEffectsOn);
REPLACE(0x005A5910, CFuncs::CFunc_SetScreenEffectsOff);
REPLACE(0x005A5930, CFuncs::CFunc_SetScreenEffectsDOFParams);
REPLACE(0x005A5950, CFuncs::CFunc_IsPal);
