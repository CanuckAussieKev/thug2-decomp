// Script commands of the engine, 0x00445000-0x00445B30: terrain sounds, models, scenes and their collision,
// animations and other assets, the system configuration and the sun's lens flare (see cfuncs_scene.h).

#include "game/script/cfuncs_scene.h"

#include <cstdint>

#include "game/ass/resource_registry.h"
#include "game/core/checksums.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/text_store.h"
#include "game/file/async.h"
#include "game/gfx/lens_flare.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/sfx/surface_kind.h"
#include "game/gfx/engine.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/engine_scene.h"
#include "game/gfx/engine_scene_list.h"
#include "game/gfx/model.h"
#include "game/gfx/model_utils.h"
#include "game/gfx/texture_dict.h"
#include "game/sys/game_config.h"

namespace ScriptCommands {

using Scripts::CScript;
using Scripts::CStruct;

int g_configNtsc{1};

namespace {

constexpr uint32_t kTerrain = 0x3789AC4E;          // "Terrain"
constexpr uint32_t kName = 0xA1DC81F9;             // "name"
constexpr uint32_t kAllowReplaceTex = 0x6C37FDC7;  // "AllowReplaceTex"
constexpr uint32_t kScene = 0x26861025;            // "scene"
constexpr uint32_t kIsNet = 0x417149C8;            // "is_net"
constexpr uint32_t kAdd = 0x02E58C18;              // "Add"
constexpr uint32_t kDescChecksum = 0x4D20D794;     // "descChecksum"
constexpr uint32_t kRefChecksum = 0x91CC92C8;      // "refChecksum"
constexpr uint32_t kParent = 0xC2719FB0;           // "parent"
constexpr uint32_t kAsync = 0x90E07C79;            // "async"
constexpr uint32_t kDesc = 0xF44A53AB;             // "desc"
constexpr uint32_t kAssertOnMissingAssets = 0x25DC7904; // "AssertOnMissingAssets"
constexpr uint32_t kUsePip = 0x23028E29;           // "use_pip"
constexpr uint32_t kLanguage = 0x2B248E4A;         // "language"
constexpr uint32_t kEnglish = 0xD37CFDFF;          // "English"
constexpr uint32_t kFrench = 0x508A31A1;           // "French"
constexpr uint32_t kGerman = 0x5DCBBF5E;           // "German"
constexpr uint32_t kItalian = 0xA8469630;          // "Italian"
constexpr uint32_t kSpanish = 0xCBE70ACB;          // "Spanish"
constexpr uint32_t kCD = 0xBA297025;               // "CD"
constexpr uint32_t kNotCD = 0x2B6D7B32;            // "NotCD"
constexpr uint32_t kGotExtraMemory = 0x38D23FD4;   // "GotExtraMemory"
constexpr uint32_t kNoExtraMemory = 0xAF11B50F;    // "NoExtraMemory"
constexpr uint32_t kPal = 0x6CAD3928;              // "Pal"
constexpr uint32_t kNTSC = 0x86137A88;             // "NTSC"
constexpr uint32_t kFramerate = 0xEE2BC65F;        // "framerate"
constexpr uint32_t kTheta = 0xF4E775F6;            // "theta"
constexpr uint32_t kPhi = 0xCD0576EE;              // "phi"
constexpr uint32_t kRadius = 0xC48391A5;           // "radius"
constexpr uint32_t kSize = 0x083FDB95;             // "size"
constexpr uint32_t kTexture = 0x7D99F28D;          // "texture"
constexpr uint32_t kCoronaTexture = 0xEB1CCC76;    // "corona_texture"
constexpr uint32_t kLensFlareData = 0x04CBD07F;    // "lens_flare_data"

// The system configuration (Config): the language (2 English, 3 French, 5 German - see str.cpp), running from the CD,
// the extra memory of a development kit, the TV standard (0 PAL, 1 NTSC) and the frame rate.
int& ConfigLanguage() { return GameConfig::g_language; }
bool& ConfigCD() { return GameConfig::g_fromCD; }
bool& ConfigGotExtraMemory() { return g_gotExtraMemory; }
int& ConfigNTSC() { return g_configNtsc; }
int& ConfigFramerate() { return GameConfig::g_frameRate; }
bool& ConfigBootstrap() { return GameConfig::g_configBootstrap; }
// The configuration's names (cdecl): the hardware, the display type for a frame rate, the language, the territory.
const char* HardwareName() { return GameConfig::GetPlatformLabel(); }
const char* DisplayTypeName(int) { return GameConfig::GetScreenModeLabel(); } // (the frame rate isn't read)
const char* LanguageName() { return GameConfig::GetLocaleLabel(); }
const char* TerritoryName() { return GameConfig::GetRegionLabel(); }

// The engine's scenes (Nx, cdecl): by name (0x0048C750: the name's checksum looked up, 0x0048C700), the first in the
// scene table, every one unloaded, one unloaded (0x0048C690; 1: its collision too), reloaded, geometry added to one
// (0x0048CDD0: the scene's name and the added file's). A scene's texture dictionary is at +0x88.
void* LookUpScene(const char* name) { return Engine3D::GetSceneByName(name); }
void* FirstScene() { return Engine3D::GetMainScene(); }
void UnloadAllScenes() { Engine3D::UnloadAllScenes(); }
void UnloadScene(void* scene, int flag) { Engine3D::Engine::UnloadScene(static_cast<Engine3D::CWorldScene*>(scene), flag); }
void QuickReloadScene(const char* name) { Engine3D::ReloadScene(name); }
void AddToScene(const char* scene, const char* add) { Engine3D::AddScene(scene, add); }
void UnloadTexDict(void* textures) { Engine3D::UnloadTexDict(static_cast<Engine3D::CTextureSet*>(textures)); }
// A scene's collision (thiscall): loaded from the scene's collision file (0x0049DF70), or added from another one
// (0x0049DFD0).
void LoadSceneCollision(void* scene, const char* name, bool isNet)
{
    static_cast<Engine3D::CWorldScene*>(scene)->LoadCollisionFile(name, isNet);
}
void AddSceneCollision(void* scene, const char* add) { static_cast<Engine3D::CWorldScene*>(scene)->LoadAddScene(add); }

// Models (Nx): one made (0x0048BFA0, a jump to 0x004B29C0), its model file loaded (0x004935E0, thiscall), destroyed
// again (0x0048C3C0).
void* SpawnModel(int) { return Engine3D::SpawnModel(); } // (0x0048BFA0, a jump to 0x004B29C0)
void LoadModelFile(void* model, const char* file, bool allowReplaceTex)
{
    static_cast<Engine3D::CRenderModel*>(model)->AddFileGeom(file, 0, true, 0, allowReplaceTex, false, true, false);
}
void DestroyModel(void* model) { Engine3D::Engine::DeleteGeomObject(model); }

} // namespace

// 0x00445000
bool __cdecl CFunc_LoadTerrainSounds(CStruct* params, CScript*)
{
    uint32_t terrain;
    if (!params->ReadHashValue(kTerrain, &terrain, 0))
        return false;
    SurfaceKind::LoadTerrainSounds(terrain);
    return true;
}

// 0x00445030
bool __cdecl CFunc_PreloadModel(CStruct* params, CScript*)
{
    const char* name;
    params->FetchText(kName, &name, true);
    void* model = SpawnModel(1);
    bool allowReplaceTex = params->HasBareFlag(kAllowReplaceTex);
    TextUtil::Chars file;
    {
        // <name>.mdl (0x004879D0 returns a String: made in `storage`)
        alignas(TextUtil::Chars) uint8_t storage[sizeof(TextUtil::Chars)];
        auto* path = Gfx2D::MakeModelPath(reinterpret_cast<TextUtil::Chars*>(storage), name, ".mdl");
        file = *path;
        path->~Chars();
    }
    LoadModelFile(model, file.c_str(), allowReplaceTex);
    DestroyModel(model);
    return true;
}

// 0x00445120
bool __cdecl CFunc_UnloadAllLevelGeometry(CStruct*, CScript*)
{
    UnloadAllScenes();
    return true;
}

// 0x00445320 (the scene isn't checked: a name that isn't loaded crashes, as in the original)
bool __cdecl CFunc_LoadCollision(CStruct* params, CScript*)
{
    const char* name = nullptr;
    params->FetchText(kScene, &name, false);
    if (name) {
        bool isNet = params->HasBareFlag(kIsNet);
        LoadSceneCollision(LookUpScene(name), name, isNet);
    }
    return true;
}

// 0x00445380
bool __cdecl CFunc_AddCollision(CStruct* params, CScript*)
{
    const char* scene = nullptr;
    const char* add = nullptr;
    params->FetchText(kScene, &scene, false);
    params->FetchText(kAdd, &add, false);
    if (scene && add)
        AddSceneCollision(LookUpScene(scene), add);
    return true;
}

// 0x004453F0
bool __cdecl CFunc_QuickReload(CStruct* params, CScript*)
{
    const char* name = nullptr;
    params->FetchText(kScene, &name, false);
    if (name)
        QuickReloadScene(name);
    return true;
}

// 0x00445430
bool __cdecl CFunc_UnloadAnim(CStruct* params, CScript*)
{
    AssetStore::CResourceRegistry* assets = AssetStore::AssetManager();
    const char* name = nullptr;
    params->FetchText(kName, &name, true);
    uint32_t desc = 0;
    uint32_t reference = 0;
    params->ReadHashValue(kDescChecksum, &desc, 1);
    if (!params->ReadHashValue(kRefChecksum, &reference, 0))
        reference = static_cast<uint32_t>(assets->m_unknown18); // the reference checksum (SetReferenceChecksum)
    uint32_t checksum = desc ? reference + desc : Checksums::HashFromText(name);
    if (AssetStore::CResource* asset = assets->ObtainLoadedItem(checksum, 0))
        assets->DiscardLoadedItem(asset);
    return true;
}

// 0x004454E0 (the script's params are passed on as the load's extra value)
bool __cdecl CFunc_LoadAsset(CStruct* params, CScript*)
{
    AssetStore::CResourceRegistry* assets = AssetStore::AssetManager();
    uint32_t parent = 0;
    const char* name;
    if (!params->FetchText(kName, &name, false))
        params->FetchText(0u, &name, true);
    params->ReadHashValue(kParent, &parent, false);
    bool async = params->HasBareFlag(kAsync);
    uint32_t desc = 0;
    params->ReadHashValue(kDesc, &desc, false);
    if (!assets->FetchOrLoadResource(name, 0, desc, parent, async, false, assets->GetPermanentByDefault(), nullptr,
                                static_cast<int>(reinterpret_cast<uintptr_t>(params))))
        Scripts::FetchIntValue(kAssertOnMissingAssets); // (an assert on it compiled out)
    return true;
}

// 0x004455A0
bool __cdecl CFunc_LoadAnim(CStruct* params, CScript*)
{
    AssetStore::CResourceRegistry* assets = AssetStore::AssetManager();
    const char* name;
    uint32_t desc = 0;
    params->FetchText(kName, &name, false);
    params->ReadHashValue(kDescChecksum, &desc, true);
    bool async = params->HasBareFlag(kAsync);
    bool usePip = params->HasBareFlag(kUsePip);
    return assets->LoadAnim(name, desc, static_cast<uint32_t>(assets->m_unknown18), async, usePip);
}

// 0x00445640
bool __cdecl CFunc_LoadSkeleton(CStruct* params, CScript*)
{
    AssetStore::CResourceRegistry* assets = AssetStore::AssetManager();
    const char* name;
    uint32_t checksum;
    params->FetchText(kName, &name, true);
    params->ReadHashValue(0u, &checksum, 1);
    assets->FetchOrLoadResource(name, 0, checksum, 0, false, false, assets->GetPermanentByDefault(), nullptr, 0);
    return true;
}

// 0x004456A0
bool __cdecl CFunc_SetReferenceChecksum(CStruct* params, CScript*)
{
    AssetStore::CResourceRegistry* assets = AssetStore::AssetManager();
    uint32_t checksum = 0; // (uninitialised in the original when neither is given; the getter asserts)
    if (!params->ReadHashValue(0u, &checksum, 0))
        params->FetchIntValue(0u, reinterpret_cast<int*>(&checksum), true);
    assets->SetUnknown18(static_cast<int>(checksum));
    return true;
}

// 0x004456F0
bool __cdecl CFunc_SetDefaultPermanent(CStruct* params, CScript*)
{
    AssetStore::CResourceRegistry* assets = AssetStore::AssetManager();
    int permanent = 0; // (uninitialised in the original without one; the getter asserts)
    params->FetchIntValue(0u, &permanent, true);
    assets->SetPermanentByDefault(permanent != 0);
    return true;
}

// 0x00445720
bool __cdecl CFunc_AddScene(CStruct* params, CScript*)
{
    const char* scene = nullptr;
    const char* add = nullptr;
    params->FetchText(kScene, &scene, false);
    params->FetchText(kAdd, &add, false);
    if (scene && add) {
        MemSys::Hub* manager = MemSys::Hub::Sole();
        manager->EnterHeapScope(manager->m_debugHeap);
        AddToScene(scene, add);
        MemSys::Hub::Sole()->LeaveHeapScope();
    }
    return true;
}

// 0x004457B0
bool __cdecl CFunc_UnloadScene(CStruct* params, CScript*)
{
    const char* name = nullptr;
    params->FetchText(kScene, &name, false);
    bool found = false;
    if (name) {
        void* scene = LookUpScene(name);
        if (scene)
            found = true;
        else
            scene = FirstScene();
        if (scene) {
            void* textures = *reinterpret_cast<void**>(static_cast<uint8_t*>(scene) + 0x88);
            UnloadScene(scene, 1);
            if (textures)
                UnloadTexDict(textures);
        }
    }
    return found;
}

// 0x00445820 (Italian picks the German setting and Spanish the French one)
bool __cdecl CFunc_SetConfig(CStruct* params, CScript*)
{
    uint32_t language = 0;
    params->ReadHashValue(kLanguage, &language, 0);
    switch (language) {
    case kItalian:
    case kGerman:
        ConfigLanguage() = 5;
        break;
    case kFrench:
    case kSpanish:
        ConfigLanguage() = 3;
        break;
    case kEnglish:
        ConfigLanguage() = 2;
        break;
    default:
        break;
    }
    if (params->HasBareFlag(kCD)) {
        if (!ConfigCD())
            FileLoad::AsyncInit(); // (passed "DATA", unused)
        ConfigCD() = true;
    }
    if (params->HasBareFlag(kNotCD)) {
        // (with the CD set, a compiled-out call: 0x005480A0)
        ConfigCD() = false;
    }
    if (params->HasBareFlag(kGotExtraMemory))
        ConfigGotExtraMemory() = true;
    if (params->HasBareFlag(kNoExtraMemory))
        ConfigGotExtraMemory() = false;
    if (params->HasBareFlag(kPal))
        ConfigNTSC() = 0;
    if (params->HasBareFlag(kNTSC))
        ConfigNTSC() = 1;
    int framerate = 60;
    if (params->FetchIntValue(kFramerate, &framerate, 0))
        ConfigFramerate() = framerate;
    return true;
}

// 0x00445980
bool __cdecl CFunc_PrintConfig(CStruct*, CScript*)
{
    DebugOut::Printf("\n");
    DebugOut::Printf("System Configuration:\n\n");
    DebugOut::Printf("Hardware       = %s\n", HardwareName());
    // (the original passes no number: the %d reads the frame rate it passed to the name's function, still on the
    // stack)
    int framerate = ConfigFramerate();
    DebugOut::Printf("Display Type   = %s, %d fps\n", DisplayTypeName(framerate), framerate);
    DebugOut::Printf("Language       = %s\n", LanguageName());
    DebugOut::Printf("Territory      = %s\n", TerritoryName());
    DebugOut::Printf("CD             = %s\n", ConfigCD() ? "True" : "False");
    DebugOut::Printf("GotExtraMemory = %s\n", ConfigGotExtraMemory() ? "True" : "False");
    DebugOut::Printf("\n");
    return true;
}

// 0x00445A30
bool __cdecl CFunc_Bootstrap(CStruct*, CScript*)
{
    return ConfigBootstrap();
}

// 0x00445A40
bool __cdecl CFunc_InitSun(CStruct* params, CScript*)
{
    float theta = 0.0f;
    float phi = 0.0f;
    float radius = 0.0f;
    float size = 0.0f;
    uint32_t texture = 0;
    uint32_t coronaTexture = 0;
    uint32_t lensFlareData = 0;
    params->FetchFloat(kTheta, &theta, 1);
    params->FetchFloat(kPhi, &phi, 1);
    params->FetchFloat(kRadius, &radius, 1);
    params->FetchFloat(kSize, &size, 1);
    params->ReadHashValue(kTexture, &texture, 1);
    params->ReadHashValue(kCoronaTexture, &coronaTexture, 1);
    params->ReadHashValue(kLensFlareData, &lensFlareData, 1);
    Sun::PrepareAll(theta, phi, radius, size, texture, coronaTexture, lensFlareData);
    return true;
}

// 0x00445B30
bool __cdecl CFunc_UpdateLensFlare(CStruct* params, CScript* script)
{
    Sun::UpdateFlare(params, script);
    return true;
}

} // namespace ScriptCommands
