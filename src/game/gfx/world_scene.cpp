#include "game/gfx/world_scene.h"
#include "game/gfx/engine_scene_list.h"
#include "game/gfx/texture_dict.h"

#include <cstdint>
#include <cstdio>

#include "game/core/checksums.h"
#include "game/core/dbg.h"
#include "game/core/str.h"
#include "game/front/level_editor_layout.h"
#include "game/script/params.h"

namespace Engine3D {

namespace {

constexpr uint32_t kScene = 0x26861025;           // scene
constexpr uint32_t kIsDictionary = 0x3D559E7D;    // is_dictionary
constexpr uint32_t kIsSky = 0xB5BD28D8;           // is_sky
constexpr uint32_t kIsNet = 0x417149C8;           // is_net
constexpr uint32_t kNoSuperSectors = 0x8943DBD8;  // no_supersectors
constexpr uint32_t kAddSuperSectors = 0x9E6EA452; // add_super_sectors
constexpr uint32_t kTexDict = 0xDC8116B2;         // tex_dict

// 0x0049F0C0 (cdecl): a texture dictionary from its file; the 1 marks a level's.
void* ReadTextureDictionary(const char* path, int level, int unknown2, int unknown3, int unknown4)
{
    return LoadTexDict(path, level, static_cast<uint32_t>(unknown2), static_cast<uint32_t>(unknown3),
                       static_cast<uint32_t>(unknown4));
}

// 0x0048CCB0 (cdecl): the engine's scene load (the geometry file, then the scene registered under its name and
// put in the scene list); returns the scene.
void* LoadSceneFile(const char* name, void* texDict, bool addSuperSectors, bool isSky, bool isDictionary, bool isNet)
{
    return LoadScene(name, static_cast<CTextureSet*>(texDict), addSuperSectors, isSky, isDictionary, isNet);
}

} // namespace

// 0x00445130
bool __cdecl CFunc_LoadScene(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    bool isDictionary = params->HasBareFlag(kIsDictionary);
    bool isSky = params->HasBareFlag(kIsSky);
    bool isNet = params->HasBareFlag(kIsNet);
    bool addSuperSectors = !params->HasBareFlag(kNoSuperSectors);
    const char* name = nullptr;
    params->FetchText(kScene, &name, 1);
    DebugOut::Printf("Loading Scene %s ...", name);

    // (the original sprintf's into the 128 bytes unchecked; the empty string is a platform suffix)
    char texturePath[128];
    std::snprintf(texturePath, sizeof(texturePath), "levels\\%s\\%s%s.tex", name, name, "");
    void* texDict = ReadTextureDictionary(texturePath, 1, 0, 0, 0);

    // A structure describing the load is built here and never used (left from an older interface).
    alignas(4) uint8_t memory[8];
    Scripts::CStruct* description = reinterpret_cast<Scripts::CStruct*>(memory)->PrepareAll();
    description->PutNameKey(kTexDict, Checksums::HashFromText(texturePath));
    if (TextUtil::FindCaseless(name, "_sky") || isSky) {
        isSky = true;
        addSuperSectors = false;
        description->PutNameKey(0u, kIsSky);
    }
    if (isDictionary) {
        addSuperSectors = false;
        description->PutNameKey(0u, kIsDictionary);
    } else if (addSuperSectors) {
        description->PutNameKey(0u, kAddSuperSectors);
    }
    if (isNet)
        description->PutNameKey(0u, kIsNet);

    LoadSceneFile(name, texDict, addSuperSectors, isSky, isDictionary, isNet);
    DebugOut::Printf("... done\n");
    description->Clear();

    // Not original - the level editor's saved layouts (level_editor_layout.h): put back once the level is up.
    MenuSystem::LevelEditor::Layout::OnSceneLoaded(name, isNet);
    return true;
}

} // namespace Nx
