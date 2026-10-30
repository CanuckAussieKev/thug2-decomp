// Cutscenes: starting and ending one (the details' InitFromStructure / End), the data's assets from the loaded
// bundle and its destructor (0x00466620-0x00467707). See cutscene.h.

#include <cstdio>
#include <cstring>

#include "game/gfx/boned_anim_frame_data.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/flexible_particle_manager.h"
#include "game/gfx/frame_source.h"
#include "game/gfx/light_manager.h"
#include "game/gfx/loading_bar_cfuncs.h"
#include "game/gfx/model_appearance.h"
#include "game/gfx/model_utils.h"
#include "game/gfx/scene_lights.h"
#include "game/skater/skater.h"
#include "game/skate/skate_module.h"
#include "game/ass/asset.h"
#include "game/ass/assman.h"
#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/file/asset_bundle.h"
#include "game/obj/cutscene.h"
#include "game/obj/node_objects.h"
#include "game/pcm/music.h"
#include "game/pcm/music_api.h"
#include "game/script/cscript.h"
#include "game/script/load.h"
#include "game/script/struct.h"
#include "game/script/struct_buffer.h"
#include "game/gfx/loading_console.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/gfx/engine.h"

namespace Obj {

GAME_GLOBAL(uint8_t[80], g_skinColourTable, 0x0067D810, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 63, 239, 226, 125, 59, 80, 171, 108, 64, 205, 4, 143, 63, 0, 0, 128, 63, 239, 226, 125, 59, 180, 212, 5, 65, 87, 207, 247, 63, 0, 0, 128, 63, 153, 71, 126, 59, 185, 169, 157, 64, 74, 151, 190, 64, 0, 0, 128, 63, 153, 71, 126, 59, 46, 84, 117, 64, 72, 24, 102, 64, 0, 0, 128, 63);
GAME_GLOBAL(float[20], g_skinColours, 0x0067D860, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);

namespace {

constexpr uint32_t kUnnamed = 0;
constexpr uint32_t kFlagParam = 0x82C84419;

// InitFromStructure's parameters and scripts (names unknown).
constexpr uint32_t kCutsceneName = 0xA1DC81F9;
constexpr uint32_t kNoMusicFlag = 0xBAFA9F1C;
constexpr uint32_t kForceFlag = 0x7DDDE00E;
constexpr uint32_t kStartTime = 0xAFD20A78;    // milliseconds
constexpr uint32_t kLoopLength = 0x45F71FC9;   // milliseconds
constexpr uint32_t kEndTime = 0xF1948C44;

// The asset types in a cutscene's bundle.
constexpr uint32_t kTypeStructure = 0x508AE2F2;   // the parameters
constexpr uint32_t kTypeQB = 0x2BBEA5C3;
constexpr uint32_t kTypeModel = 0xFD8697E1;       // (also 0xE7308C1F and 0x0524FD4E)
constexpr uint32_t kTypeSkin = 0xE7308C1F;
constexpr uint32_t kTypeModel2 = 0x0524FD4E;
constexpr uint32_t kTypeCameraAnim = 0x05CA1497;
constexpr uint32_t kTypeTextures = 0x1512808D;
constexpr uint32_t kTypeColours = 0x2CD4107D;
constexpr uint32_t kTypeObjectAnim = 0x2E4BF21B;
constexpr uint32_t kTypeImage = 0xEDD8D75F;
constexpr uint32_t kTypeObjects = 0x5AC14717;
constexpr uint32_t kTypeAnim = 0xEAB51346;
constexpr uint32_t kTypeExtra = 0xFFC529F4;
// The skater's appearance parts and the colours a skinned model takes from them.
constexpr uint32_t kPartA = 0x650FAB6D, kPartB = 0x0FC85BAE;
constexpr uint32_t kColourNames[4] = {0xA28FAB7F, 0xB90F08D0, 0x1DBCFAE8, 0x0442D3C1};

Ass::CAssMan* Assets() { return Ass::g_assetManager; }
// The asset manager's load (0x00408060: not permanent, copied), unload (0x00407B90 + 0x00407B20) and find.
void* LoadAssetData(uint32_t name, int type, uint32_t textures, const void* data, uint32_t size, void* params)
{
    return Assets()->LoadAssetFromStream(name, type, textures, const_cast<void*>(data), size, false, true, params, 0);
}
void UnloadAsset(uint32_t name)
{
    Ass::CAsset* asset = Assets()->GetAsset(name, 1);
    Assets()->UnloadAsset(asset);
}
void DeleteVirtual(void* object)
{
    reinterpret_cast<void(__thiscall*)(void*, int)>((*static_cast<void***>(object))[0])(object, 1);
}
void* NewAnimSource()
{
    void* memory = AnimSourceAllocate(0x214);
    return memory ? static_cast<Nx::CFrameSource*>(memory)->Construct() : nullptr;
}
Script::CStruct* MakeStruct()
{
    Script::CStruct* memory = Script::NewStruct();
    return memory ? memory->Init() : nullptr;
}
void FreeStruct(Script::CStruct* s)
{
    if (!s)
        return;
    s->Clear();
    Script::DeleteStruct(s);
}
void Run(uint32_t script, Script::CStruct* params = nullptr, CObject* object = nullptr)
{
    Script::RunScript(script, params, object, false, nullptr);
}

// The asset manager's load parameters (0x00409E90 constructs them).
struct LoadParams {
    uint8_t flag0;
    uint8_t pad1[3];
    int textureSet;       // +0x04
    void* extra;          // +0x08
    uint32_t extraSize;   // +0x0C
    uint8_t isModel;      // +0x10
    uint8_t flag11;
};
// The textures' load parameters (filled in place).
struct TextureParams {
    uint8_t flag0;
    uint8_t pad1[3];
    int textureSet;       // +0x04
    uint8_t isModel;      // +0x08
    uint8_t flag9;
};
// A skinned model's colours (for 0x0048C090): the colour table, its entries, where the colours go.
struct SkinColours {
    const uint8_t* end;
    const uint8_t* start;
    void* table;          // (0x0067D810)
    float* colours;       // (0x0067D860)
};

// (debug output, compiled out)
inline void PrintCutsceneDetails(void*) {}

} // namespace

// 0x00466620
bool CCutsceneDetails::InitFromStructure(Script::CStruct* params)
{
    char fileName[256] = "none"; // (from the game's 0x0064797C)
    uint32_t name = 0;
    if (m_ownsData) {
        const char* path;
        params->GetString(kCutsceneName, &path, 1);
        name = Crc::GenerateCRCFromString(CutsceneNameFromPath(path));
        m_asset = Crc::GenerateCRCFromString(path);
        std::memcpy(fileName, path, std::strlen(path) + 1); // (unbounded, as the original)
    } else {
        params->GetChecksum(kCutsceneName, &name, 1);
        m_asset = 0;
    }
    Script::CStruct* settings = MakeStruct();
    settings->AppendStructureIfAny(params);
    reinterpret_cast<CMovieDetails*>(this)->InitFromStructure(settings);
    if (Script::CStruct* defaults = Script::GetStructure(Crc::ExtendCRCWithString(name, "_params")))
        m_params->AppendStructureIfAny(defaults);
    m_beginScript = Crc::ExtendCRCWithString(name, "_begin");
    m_endScript = Crc::ExtendCRCWithString(name, "_end");
    bool loaded = false;
    if (!m_ownsData) {
        m_data = static_cast<CCutsceneData*>(
            Assets()->GetAssetData(name, 0));
        loaded = m_data != nullptr;
    } else {
        // the loading screen; the music volumes kept to give back at the end, then muted
        Nx::DisplayLoadingScreen(nullptr, true, false);
        Gfx::LoadingConsole::ScreenShown(true, false); // not original: the loading console (gfx/loading_console.h)
        m_musicVolume = Nx::GetAmbientLightModulationFactor(true);
        m_streamVolume0 = Nx::GetDiffuseLightModulationFactor(0, true);
        m_streamVolume1 = Nx::GetDiffuseLightModulationFactor(1, true);
        Nx::SetAmbientLightModulationFactor(0.0f, 0.0f);
        Nx::SetDiffuseLightModulationFactor(0, 0.0f, 0.0f);
        Nx::SetDiffuseLightModulationFactor(1, 0.0f, 0.0f);
        Nx::UpdateSceneLightQueue();
        Ass::CAssMan* assets = Assets();
        if (!assets->GetAssetDataByName(fileName, 0)) {
            int music = m_params->ContainsFlag(kNoMusicFlag) ? 0 : 1;
            int streams = m_params->ContainsFlag(kNoMusicFlag) ? 0 : 1;
            if (params->ContainsFlag(kForceFlag)) {
                music = 1;
                streams = 1;
            }
            m_params->AddInteger(0xCDF2B13E, music);
            m_params->AddInteger(0x9A4BDFF5, streams);
            m_params->AddInteger(0xED064302, 1);
            m_params->AddInteger(0xBABF2DC9, 1);
            m_params->AppendStructureIfAny(settings);
            Run(0x722D9CA7);
            Gfx::HideModelsForCutscene();
            Run(0xE1CE826A, m_params);
            m_data = static_cast<CCutsceneData*>(assets->LoadAsset(fileName, 0, 0, 0, false, false, false, m_settings, 0));
            float start;
            if (params->GetFloat(kStartTime, &start, 0)) {
                CCutsceneData* data = m_data;
                float seconds = static_cast<float>(start * static_cast<double>(0.0010000000474974513f));
                Pcm::PauseMusic(1);
                data->m_unknown3474 = 1;
                data->m_time = seconds;
            }
            float loop;
            if (params->GetFloat(kLoopLength, &loop, 0)) {
                loop = static_cast<float>(loop * static_cast<double>(0.0010000000474974513f));
                CCutsceneData* data = m_data;
                data->m_unknown3474 = 1;
                data->m_unknown3488 = *reinterpret_cast<uint32_t*>(&data->m_time);
                data->m_unknown348C = static_cast<float>(static_cast<double>(loop) + data->m_time);
                Pcm::PauseMusic(1);
            }
            if (Script::CArray* subtitles = Script::GetArray(Crc::ExtendCRCWithString(name, "_subtitles")))
                m_data->SetSubtitles(subtitles);
            loaded = true;
        }
    }
    FreeStruct(settings);
    return loaded;
}

// 0x00466A10
void CCutsceneDetails::End()
{
    if (!m_ownsData) {
        if (m_data)
            m_data->DestroyObjects();
        if (m_endScript)
            Run(m_endScript);
        PrintCutsceneDetails(this);
        return;
    }
    Run(0xE7BC7EFD);
    Run(0x4C0FD1D7);
    reinterpret_cast<Nx::CFlexibleParticleManager*&>(Nx::g_flexibleParticleManager)->DeleteTempHeap();
    if (m_data) {
        UnloadAsset(m_asset);
        m_data = nullptr;
    }
    Nx::ClearSceneLightQueue();
    if (m_endScript)
        Run(m_endScript);
    Run(0xF49A8C9C);
    Run(0xDEFF69FF);
    Mem::Manager::Get()->DeleteCutsceneHeaps();
    auto* skater =
        static_cast<CCompositeObject*>(Mdl::Skate::Instance()->GetLocalSkater());
    if (skater) {
        Script::CStruct* flag = MakeStruct();
        flag->AddChecksum(kUnnamed, kFlagParam);
        Run(0xABF617EE, flag, reinterpret_cast<CObject*>(skater));
        FreeStruct(flag);
    }
    Run(0xBCA1791C, m_params);
    Gfx::ShowModelsAfterCutscene();
    Run(0x94D0D528);
    PrintCutsceneDetails(this);
    Run(0x15674315);
}

// 0x00466C20
void CCutsceneData::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(0x00647908);
    g_cutscenePlaying = false;
    if (mp_bundle) {
        DeleteVirtual(mp_bundle);
        mp_bundle = nullptr;
    }
    if (m_cameraSource)
        DeleteVirtual(m_cameraSource);
    m_cameraSource = nullptr;
    if (m_objectSource)
        DeleteVirtual(m_objectSource);
    m_objectSource = nullptr;
    for (int i = 0; i < m_animationCount; ++i) {
        if (m_animations[i].source)
            DeleteVirtual(m_animations[i].source);
        m_animations[i].data = nullptr; // (the source's pointer left as it is, as the original)
    }
    DestroyObjects();
    RemoveCutsceneLights();
    for (int i = 0; i < m_assetCount; ++i) {
        UnloadAsset(m_assets[i]);
        m_assets[i] = 0;
    }
    if (m_flag34B0) {
        Pcm::StopMusic();
        if (m_flag34B0) {
            Run(0x7216D8DA);
            char fileName[256];
            std::snprintf(fileName, sizeof(fileName), "%08x.qb", m_name);
            Script::UnloadQB(Crc::GenerateCRCFromString(fileName));
        }
    }
    if (mp_params) {
        FreeStruct(mp_params);
        mp_params = nullptr;
    }
}

// 0x00466DD0
void CCutsceneData::LoadFromBundle()
{
    File::AssetBundle* bundle = mp_bundle;
    int count = bundle->m_count;
    for (int i = 0; i < count; ++i) {
        File::AssetBundle::Entry* entry = mp_bundle->GetEntry(i);
        void* data = mp_bundle->GetAsset(i);
        uint32_t type = entry->type;
        switch (type) {
        case kTypeStructure:
            mp_params->Clear();
            Script::ReadFromBuffer(mp_params, static_cast<const uint8_t*>(data));
            break;
        case kTypeQB:
            if (m_flag34B0) {
                char fileName[256];
                std::snprintf(fileName, sizeof(fileName), "%08x.qb", m_name);
                Script::LoadQBData(fileName, static_cast<const uint8_t*>(data), 1, 0, 0);
            }
            break;
        case kTypeCameraAnim: {
            uint32_t name = NameChecksum("_CAM", static_cast<int>(entry->name)); // (the game's 0x00647914)
            LoadAsset(name, reinterpret_cast<uint32_t>(data), entry->size);
            ++m_cameraParts;
            if (!m_cameraSource) {
                m_cameraSource = NewAnimSource();
                static_cast<Nx::CFrameSource*>(m_cameraSource)->SetDone(true);
            }
            if (!mp_cameraAnim) {
                mp_cameraAnim = static_cast<Nx::CFrameSource*>(m_cameraSource)->SetAnim(name);
                static_cast<Gfx::CBonedAnimFrameData*>(mp_cameraAnim)->ArmEvents();
            }
            if (!mp_objectAnims)
                mp_objectAnims = mp_cameraAnim;
            break;
        }
        case kTypeObjectAnim: {
            uint32_t name = NameChecksum("_OBA", static_cast<int>(entry->name)); // (the game's 0x0064790C)
            LoadAsset(name, reinterpret_cast<uint32_t>(data), entry->size);
            ++m_objectParts;
            if (!m_objectSource) {
                m_objectSource = NewAnimSource();
                static_cast<Nx::CFrameSource*>(m_objectSource)->SetDone(true);
            }
            if (!mp_animList) {
                mp_animList = static_cast<Nx::CFrameSource*>(m_objectSource)->SetAnim(name);
                static_cast<Gfx::CBonedAnimFrameData*>(mp_animList)->ArmEvents();
            }
            break;
        }
        case kTypeImage: {
            uint32_t name = entry->name;
            LoadAssetData(name, 6, 0, data, entry->size, nullptr);
            m_assets[m_assetCount] = name;
            ++m_assetCount;
            break;
        }
        case kTypeObjects:
            mp_params->Clear();
            ObjectsFromTable(mp_params, static_cast<const uint32_t*>(data));
            break;
        case kTypeAnim:
            LoadAnimation(entry->name, reinterpret_cast<uint32_t>(data), entry->size);
            break;
        case kTypeModel:
        case kTypeSkin:
        case kTypeModel2:
            LoadModel(i, entry, data);
            continue; // (it unloads its own assets)
        case kTypeTextures:
        case kTypeColours:
        case kTypeExtra:
            continue; // (loaded with their models)
        default:
            break;
        }
        mp_bundle->FreeAsset(entry->name, entry->type);
    }
    if (!mp_params->GetFloat(kEndTime, &m_endTime, 0))
        m_endTime = *static_cast<float*>(m_flag34B0 ? mp_cameraAnim : mp_animList);
}

// A model in the bundle (with its textures, a skinned model's colours): loaded unless it's a skin variant that
// isn't the skater's (variants: name + 1 ... name + 4 by face and sex).
void CCutsceneData::LoadModel(int index, File::AssetBundle::Entry* entry, void* data)
{
    uint32_t modelName = entry->name + kTypeModel; // (the original prints name + type, compiled out)
    LoadParams params;
    reinterpret_cast<Ass::SkinParams*>(&params)->Clear(); // (the skinned model's load parameters, 0x14 bytes)
    void* textureData = mp_bundle->FindAsset(entry->name, kTypeTextures, 1);
    File::AssetBundle::Entry* textures = mp_bundle->FindEntry(entry->name, kTypeTextures, 1);
    uint32_t textureSize = textures->size;
    uint32_t texturesName = textures->type + textures->name;
    uint8_t isModel = entry->type == kTypeModel;
    params.extra = nullptr;
    params.extraSize = 0;
    params.isModel = isModel;
    params.flag0 = 0;
    params.textureSet = entry->type == kTypeSkin ? 0 : index + 200;
    TextureParams textureParams;
    textureParams.textureSet = params.textureSet;
    void* assets = Assets();
    textureParams.flag0 = 0;
    textureParams.flag9 = 0;
    textureParams.isModel = isModel;
    bool load = static_cast<int>(entry->size) >= 0x96;
    if (const uint8_t* colours =
            static_cast<const uint8_t*>(mp_bundle->FindAsset(entry->name, kTypeColours, 0))) {
        // the skater's colours from its appearance (part A or B, else the other)
        auto* part1 = static_cast<Script::CStruct*>(
            static_cast<Gfx::CModelAppearance*>(m_unknown34A8)->GetPart(kPartA));
        if (!part1)
            part1 = static_cast<Script::CStruct*>(
                static_cast<Gfx::CModelAppearance*>(m_unknown34A8)->GetPart(kPartB));
        auto* part2 = static_cast<Script::CStruct*>(
            static_cast<Gfx::CModelAppearance*>(m_unknown34A8)->GetPartOption(kPartA));
        if (!part2)
            part2 = static_cast<Script::CStruct*>(
                static_cast<Gfx::CModelAppearance*>(m_unknown34A8)->GetPartOption(kPartB));
        float* skin = g_skinColours;
        skin[0] = skin[1] = skin[2] = skin[3] = 1.0f;
        for (int k = 0; k < 4; ++k) {
            float colour[4];
            Script::CStruct* from = part1->ContainsComponentNamed(kColourNames[k]) ? part1 : part2;
            std::memcpy(skin + 4 * (k + 1), ReadColour(colour, from, kColourNames[k]), 16);
        }
        int entries = *reinterpret_cast<const int*>(colours + 4);
        SkinColours table;
        table.start = colours + 8;
        table.end = table.start + entries * 12;
        table.table = g_skinColourTable;
        table.colours = skin;
        // which skin this is: the next names along say (none after it: the last)
        bool male = false, hasFace = false;
        if (!mp_bundle->FindEntry(entry->name + 1, kTypeModel, 0)) {
            male = false;
            hasFace = true;
        } else if (!mp_bundle->FindEntry(entry->name + 2, kTypeModel, 0)) {
            male = false;
            hasFace = false;
        } else if (!mp_bundle->FindEntry(entry->name + 3, kTypeModel, 0)) {
            male = true;
            hasFace = true;
        } else if (!mp_bundle->FindEntry(entry->name + 4, kTypeModel, 0)) {
            male = true;
            hasFace = false;
        }
        void* face = AppearanceFace(m_unknown34A8);
        int skaterFace = face && GameFunction<bool(__thiscall*)(void*)>(0x00487510)(face) ? 1 : 0;
        int skaterMale;
        static_cast<Script::CStruct*>(m_unknown34AC)->GetInteger(0x3F813177, &skaterMale, 1);
        load = static_cast<int>(hasFace) == skaterFace && static_cast<int>(male) == skaterMale;
        if (load)
            GameFunction<void(__cdecl*)(SkinColours*)>(0x0048C090)(&table);
    }
    if (load) {
        params.extra = mp_bundle->FindAsset(entry->name, kTypeExtra, 0);
        if (params.extra)
            params.extraSize = mp_bundle->FindEntry(entry->name, kTypeExtra, 1)->size;
        static_cast<Ass::CAssMan*>(assets)->LoadAssetFromStream(texturesName, 3, 0, textureData, textureSize, false, true,
                                                                 &textureParams, 0);
        static_cast<Ass::CAssMan*>(assets)->LoadAssetFromStream(modelName, 7, texturesName, data, entry->size, false, true,
                                                                 &params, 0);
        m_assets[m_assetCount] = modelName;
        ++m_assetCount;
        m_assets[m_assetCount] = texturesName;
        ++m_assetCount;
    }
    mp_bundle->FreeAsset(entry->name, entry->type);
    mp_bundle->FreeAsset(entry->name, kTypeTextures);
    mp_bundle->FreeAsset(entry->name, kTypeExtra);
    mp_bundle->FreeAsset(entry->name, kTypeColours);
}

} // namespace Obj

REPLACE(0x00466620, &Obj::CCutsceneDetails::InitFromStructure);
REPLACE(0x00466A10, &Obj::CCutsceneDetails::End);
REPLACE(0x00466C20, &Obj::CCutsceneData::DestroyBody);
REPLACE(0x00466DD0, &Obj::CCutsceneData::LoadFromBundle);
