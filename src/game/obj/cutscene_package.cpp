// Cutscenes: starting and ending one (the details' ConfigureFromData / End), the data's assets from the loaded
// bundle and its destructor (0x00466620-0x00467707). See cutscene.h.

#include <cstdint>
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
#include "game/core/code_address.h"
#include "game/gfx/engine.h"
#include "game/obj/cutscene.h"

namespace GameObjects {

uint8_t g_skinColourTable[80]{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 63, 239, 226, 125, 59, 80, 171, 108, 64, 205, 4, 143, 63, 0, 0, 128, 63, 239, 226, 125, 59, 180, 212, 5, 65, 87, 207, 247, 63, 0, 0, 128, 63, 153, 71, 126, 59, 185, 169, 157, 64, 74, 151, 190, 64, 0, 0, 128, 63, 153, 71, 126, 59, 46, 84, 117, 64, 72, 24, 102, 64, 0, 0, 128, 63};
float g_skinColours[20]{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

namespace {

constexpr uint32_t kUnnamed = 0;
constexpr uint32_t kFlagParam = 0x82C84419;

// ConfigureFromData's parameters and scripts (names unknown).
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

AssetStore::CResourceRegistry* Assets() { return AssetStore::g_assetManager; }
// The asset manager's load (0x00408060: not permanent, copied), unload (0x00407B90 + 0x00407B20) and find.
void* LoadAssetData(uint32_t name, int type, uint32_t textures, const void* data, uint32_t size, void* params)
{
    return Assets()->ReadResourceFromStream(name, type, textures, const_cast<void*>(data), size, false, true, params, 0);
}
void DiscardLoadedItem(uint32_t name)
{
    AssetStore::CResource* asset = Assets()->ObtainLoadedItem(name, 1);
    Assets()->DiscardLoadedItem(asset);
}
void DeleteVirtual(void* object)
{
    reinterpret_cast<void(__thiscall*)(void*, int)>((*static_cast<void***>(object))[0])(object, 1);
}
void* NewAnimSource()
{
    void* memory = AnimSourceAllocate(0x214);
    return memory ? static_cast<Engine3D::CFrameSource*>(memory)->MakeNew() : nullptr;
}
Scripts::CStruct* MakeStruct()
{
    Scripts::CStruct* memory = Scripts::NewStruct();
    return memory ? memory->PrepareAll() : nullptr;
}
void FreeStruct(Scripts::CStruct* s)
{
    if (!s)
        return;
    s->Clear();
    Scripts::DeleteStruct(s);
}
void Run(uint32_t script, Scripts::CStruct* params = nullptr, CEntity* object = nullptr)
{
    Scripts::LaunchScript(script, params, object, false, nullptr);
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
bool CCinematicEntry::ConfigureFromData(Scripts::CStruct* params)
{
    char fileName[256] = "none"; // (from the game's 0x0064797C)
    uint32_t name = 0;
    if (m_ownsData) {
        const char* path;
        params->FetchText(kCutsceneName, &path, 1);
        name = Checksums::HashFromText(CutsceneNameFromPath(path));
        m_asset = Checksums::HashFromText(path);
        std::memcpy(fileName, path, std::strlen(path) + 1); // (unbounded, as the original)
    } else {
        params->ReadHashValue(kCutsceneName, &name, 1);
        m_asset = 0;
    }
    Scripts::CStruct* settings = MakeStruct();
    settings->AppendStructureIfAny(params);
    reinterpret_cast<CVideoEntry*>(this)->ConfigureFromData(settings);
    if (Scripts::CStruct* defaults = Scripts::ReadSubStruct(Checksums::FoldTextIntoChecksum(name, "_params")))
        m_args->AppendStructureIfAny(defaults);
    m_beginScript = Checksums::FoldTextIntoChecksum(name, "_begin");
    m_endScript = Checksums::FoldTextIntoChecksum(name, "_end");
    bool loaded = false;
    if (!m_ownsData) {
        m_content = static_cast<CCinematicTable*>(
            Assets()->GetAssetData(name, 0));
        loaded = m_content != nullptr;
    } else {
        // the loading screen; the music volumes kept to give back at the end, then muted
        Engine3D::DisplayLoadingScreen(nullptr, true, false);
        Gfx2D::LoadingConsole::ScreenShown(true, false); // not original: the loading console (gfx/loading_console.h)
        m_musicVolume = Engine3D::GetAmbientLightModulationFactor(true);
        m_streamVolume0 = Engine3D::GetDiffuseLightModulationFactor(0, true);
        m_streamVolume1 = Engine3D::GetDiffuseLightModulationFactor(1, true);
        Engine3D::SetAmbientLightModulationFactor(0.0f, 0.0f);
        Engine3D::SetDiffuseLightModulationFactor(0, 0.0f, 0.0f);
        Engine3D::SetDiffuseLightModulationFactor(1, 0.0f, 0.0f);
        Engine3D::UpdateSceneLightQueue();
        AssetStore::CResourceRegistry* assets = Assets();
        if (!assets->GetAssetDataByName(fileName, 0)) {
            int music = m_args->HasBareFlag(kNoMusicFlag) ? 0 : 1;
            int streams = m_args->HasBareFlag(kNoMusicFlag) ? 0 : 1;
            if (params->HasBareFlag(kForceFlag)) {
                music = 1;
                streams = 1;
            }
            m_args->PutInt(0xCDF2B13E, music);
            m_args->PutInt(0x9A4BDFF5, streams);
            m_args->PutInt(0xED064302, 1);
            m_args->PutInt(0xBABF2DC9, 1);
            m_args->AppendStructureIfAny(settings);
            Run(0x722D9CA7);
            Gfx2D::HideModelsForCutscene();
            Run(0xE1CE826A, m_args);
            m_content = static_cast<CCinematicTable*>(assets->LoadAsset(fileName, 0, 0, 0, false, false, false, m_settings, 0));
            float start;
            if (params->FetchFloat(kStartTime, &start, 0)) {
                CCinematicTable* data = m_content;
                float seconds = static_cast<float>(start * static_cast<double>(0.0010000000474974513f));
                AudioStream::PauseMusic(1);
                data->m_unknown3474 = 1;
                data->m_clock = seconds;
            }
            float loop;
            if (params->FetchFloat(kLoopLength, &loop, 0)) {
                loop = static_cast<float>(loop * static_cast<double>(0.0010000000474974513f));
                CCinematicTable* data = m_content;
                data->m_unknown3474 = 1;
                data->m_unknown3488 = *reinterpret_cast<uint32_t*>(&data->m_clock);
                data->m_unknown348C = static_cast<float>(static_cast<double>(loop) + data->m_clock);
                AudioStream::PauseMusic(1);
            }
            if (Scripts::CArray* subtitles = Scripts::FetchList(Checksums::FoldTextIntoChecksum(name, "_subtitles")))
                m_content->SetSubtitles(subtitles);
            loaded = true;
        }
    }
    FreeStruct(settings);
    return loaded;
}

// 0x00466A10
void CCinematicEntry::End()
{
    if (!m_ownsData) {
        if (m_content)
            m_content->DestroyObjects();
        if (m_endScript)
            Run(m_endScript);
        PrintCutsceneDetails(this);
        return;
    }
    Run(0xE7BC7EFD);
    Run(0x4C0FD1D7);
    reinterpret_cast<Engine3D::CFlexibleParticleManager*&>(Engine3D::g_flexibleParticleManager)->DeleteTempHeap();
    if (m_content) {
        DiscardLoadedItem(m_asset);
        m_content = nullptr;
    }
    Engine3D::ClearSceneLightQueue();
    if (m_endScript)
        Run(m_endScript);
    Run(0xF49A8C9C);
    Run(0xDEFF69FF);
    MemSys::Hub::Pull()->DeleteCutsceneHeaps();
    auto* skater =
        static_cast<CGameEntity*>(GameModules::Skate::Sole()->GetHomeRider());
    if (skater) {
        Scripts::CStruct* flag = MakeStruct();
        flag->PutNameKey(kUnnamed, kFlagParam);
        Run(0xABF617EE, flag, reinterpret_cast<CEntity*>(skater));
        FreeStruct(flag);
    }
    Run(0xBCA1791C, m_args);
    Gfx2D::ShowModelsAfterCutscene();
    Run(0x94D0D528);
    PrintCutsceneDetails(this);
    Run(0x15674315);
}

// 0x00466C20
void CCinematicTable::DestroyBody()
{
    m_vtable = g_cutsceneDataVtable;
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
        DiscardLoadedItem(m_assets[i]);
        m_assets[i] = 0;
    }
    if (m_flag34B0) {
        AudioStream::StopMusic();
        if (m_flag34B0) {
            Run(0x7216D8DA);
            char fileName[256];
            std::snprintf(fileName, sizeof(fileName), "%08x.qb", m_label);
            Scripts::UnloadQB(Checksums::HashFromText(fileName));
        }
    }
    if (mp_params) {
        FreeStruct(mp_params);
        mp_params = nullptr;
    }
}

// 0x00466DD0
void CCinematicTable::LoadFromBundle()
{
    FileLoad::AssetBundle* bundle = mp_bundle;
    int count = bundle->m_num;
    for (int i = 0; i < count; ++i) {
        FileLoad::AssetBundle::TableRow* entry = mp_bundle->GetEntry(i);
        void* data = mp_bundle->ObtainLoadedItem(i);
        uint32_t type = entry->type;
        switch (type) {
        case kTypeStructure:
            mp_params->Clear();
            Scripts::DecodeFromBytes(mp_params, static_cast<const uint8_t*>(data));
            break;
        case kTypeQB:
            if (m_flag34B0) {
                char fileName[256];
                std::snprintf(fileName, sizeof(fileName), "%08x.qb", m_label);
                Scripts::LoadQBData(fileName, static_cast<const uint8_t*>(data), 1, 0, 0);
            }
            break;
        case kTypeCameraAnim: {
            uint32_t name = HashOfName("_CAM", static_cast<int>(entry->name)); // (the game's 0x00647914)
            LoadAsset(name, reinterpret_cast<uint32_t>(data), entry->size);
            ++m_cameraParts;
            if (!m_cameraSource) {
                m_cameraSource = NewAnimSource();
                static_cast<Engine3D::CFrameSource*>(m_cameraSource)->SetDone(true);
            }
            if (!mp_cameraAnim) {
                mp_cameraAnim = static_cast<Engine3D::CFrameSource*>(m_cameraSource)->PlayClip(name);
                static_cast<Gfx2D::CJointClipFrames*>(mp_cameraAnim)->ArmEvents();
            }
            if (!mp_objectAnims)
                mp_objectAnims = mp_cameraAnim;
            break;
        }
        case kTypeObjectAnim: {
            uint32_t name = HashOfName("_OBA", static_cast<int>(entry->name)); // (the game's 0x0064790C)
            LoadAsset(name, reinterpret_cast<uint32_t>(data), entry->size);
            ++m_objectParts;
            if (!m_objectSource) {
                m_objectSource = NewAnimSource();
                static_cast<Engine3D::CFrameSource*>(m_objectSource)->SetDone(true);
            }
            if (!mp_animList) {
                mp_animList = static_cast<Engine3D::CFrameSource*>(m_objectSource)->PlayClip(name);
                static_cast<Gfx2D::CJointClipFrames*>(mp_animList)->ArmEvents();
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
            IngestMesh(i, entry, data);
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
    if (!mp_params->FetchFloat(kEndTime, &m_endTime, 0))
        m_endTime = *static_cast<float*>(m_flag34B0 ? mp_cameraAnim : mp_animList);
}

// A model in the bundle (with its textures, a skinned model's colours): loaded unless it's a skin variant that
// isn't the skater's (variants: name + 1 ... name + 4 by face and sex).
void CCinematicTable::IngestMesh(int index, FileLoad::AssetBundle::TableRow* entry, void* data)
{
    uint32_t modelName = entry->name + kTypeModel; // (the original prints name + type, compiled out)
    LoadParams params;
    reinterpret_cast<AssetStore::SkinParams*>(&params)->Clear(); // (the skinned model's load parameters, 0x14 bytes)
    void* textureData = mp_bundle->FindAsset(entry->name, kTypeTextures, 1);
    FileLoad::AssetBundle::TableRow* textures = mp_bundle->FindEntry(entry->name, kTypeTextures, 1);
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
        auto* part1 = static_cast<Scripts::CStruct*>(
            static_cast<Gfx2D::CModelLook*>(m_unknown34A8)->GetPart(kPartA));
        if (!part1)
            part1 = static_cast<Scripts::CStruct*>(
                static_cast<Gfx2D::CModelLook*>(m_unknown34A8)->GetPart(kPartB));
        auto* part2 = static_cast<Scripts::CStruct*>(
            static_cast<Gfx2D::CModelLook*>(m_unknown34A8)->GetPartOption(kPartA));
        if (!part2)
            part2 = static_cast<Scripts::CStruct*>(
                static_cast<Gfx2D::CModelLook*>(m_unknown34A8)->GetPartOption(kPartB));
        float* skin = g_skinColours;
        skin[0] = skin[1] = skin[2] = skin[3] = 1.0f;
        for (int k = 0; k < 4; ++k) {
            float colour[4];
            Scripts::CStruct* from = part1->HasEntryNamed(kColourNames[k]) ? part1 : part2;
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
        int skaterFace = face && reinterpret_cast<bool(__thiscall*)(void*)>(CodeAddress(&Gfx2D::CFaceSkin::IsSound))(face) ? 1 : 0;
        int skaterMale;
        static_cast<Scripts::CStruct*>(m_unknown34AC)->FetchIntValue(0x3F813177, &skaterMale, 1);
        load = static_cast<int>(hasFace) == skaterFace && static_cast<int>(male) == skaterMale;
        if (load)
            Engine3D::SetScreenClip(&table);
    }
    if (load) {
        params.extra = mp_bundle->FindAsset(entry->name, kTypeExtra, 0);
        if (params.extra)
            params.extraSize = mp_bundle->FindEntry(entry->name, kTypeExtra, 1)->size;
        static_cast<AssetStore::CResourceRegistry*>(assets)->ReadResourceFromStream(texturesName, 3, 0, textureData, textureSize, false, true,
                                                                 &textureParams, 0);
        static_cast<AssetStore::CResourceRegistry*>(assets)->ReadResourceFromStream(modelName, 7, texturesName, data, entry->size, false, true,
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
