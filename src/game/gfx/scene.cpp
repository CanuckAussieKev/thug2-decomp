#include "game/gfx/scene.h"

#include <cstdint>
#include <cstdio>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/str.h"
#include "game/front/level_editor_layout.h"
#include "game/front/menu_backdrop.h"
#include "game/script/params.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kScene = 0x26861025;           // scene
constexpr uint32_t kIsDictionary = 0x3D559E7D;    // is_dictionary
constexpr uint32_t kIsSky = 0xB5BD28D8;           // is_sky
constexpr uint32_t kIsNet = 0x417149C8;           // is_net
constexpr uint32_t kNoSuperSectors = 0x8943DBD8;  // no_supersectors
constexpr uint32_t kAddSuperSectors = 0x9E6EA452; // add_super_sectors
constexpr uint32_t kTexDict = 0xDC8116B2;         // tex_dict

// 0x0049F0C0 (cdecl): a texture dictionary from its file; the 1 marks a level's.
void* LoadTextureDictionary(const char* path, int level, int unknown2, int unknown3, int unknown4)
{
    return GameFunction<void*(__cdecl*)(const char*, int, int, int, int)>(0x0049F0C0)(path, level, unknown2, unknown3,
                                                                                    unknown4);
}

// 0x0048CCB0 (cdecl): the engine's scene load (the geometry file, then the scene registered under its name and
// put in the scene list); returns the scene.
void* LoadSceneFile(const char* name, void* texDict, bool addSuperSectors, bool isSky, bool isDictionary, bool isNet)
{
    return GameFunction<void*(__cdecl*)(const char*, void*, bool, bool, bool, bool)>(0x0048CCB0)(
        name, texDict, addSuperSectors, isSky, isDictionary, isNet);
}

} // namespace

// 0x00445130
bool __cdecl CFunc_LoadScene(Script::CStruct* params, Script::CScript* /*script*/)
{
    bool isDictionary = params->ContainsFlag(kIsDictionary);
    bool isSky = params->ContainsFlag(kIsSky);
    bool isNet = params->ContainsFlag(kIsNet);
    bool addSuperSectors = !params->ContainsFlag(kNoSuperSectors);
    const char* name = nullptr;
    params->GetString(kScene, &name, 1);
    Dbg::Printf("Loading Scene %s ...", name);

    // (the original sprintf's into the 128 bytes unchecked; the empty string is a platform suffix)
    char texturePath[128];
    std::snprintf(texturePath, sizeof(texturePath), "levels\\%s\\%s%s.tex", name, name, "");
    void* texDict = LoadTextureDictionary(texturePath, 1, 0, 0, 0);

    // A structure describing the load is built here and never used (left from an older interface).
    alignas(4) uint8_t memory[8];
    Script::CStruct* description = reinterpret_cast<Script::CStruct*>(memory)->Init();
    description->AddChecksum(kTexDict, Crc::GenerateCRCFromString(texturePath));
    if (Str::FindCaseless(name, "_sky") || isSky) {
        isSky = true;
        addSuperSectors = false;
        description->AddChecksum(0u, kIsSky);
    }
    if (isDictionary) {
        addSuperSectors = false;
        description->AddChecksum(0u, kIsDictionary);
    } else if (addSuperSectors) {
        description->AddChecksum(0u, kAddSuperSectors);
    }
    if (isNet)
        description->AddChecksum(0u, kIsNet);

    LoadSceneFile(name, texDict, addSuperSectors, isSky, isDictionary, isNet);
    Dbg::Printf("... done\n");
    description->Clear();

    // Not original - the menu_backdrop feature (see menu_backdrop.h): the front end's scene brings the backdrop's.
    // Original behaviour: only the scene asked for is loaded; with menu_backdrop "mainmenu" also loads THPS4's skate
    // shop (its sky and scene).
    Front::MenuBackdrop::OnSceneLoaded(name, isNet);
    // Not original - the level editor's saved layouts (level_editor_layout.h): put back once the level is up.
    Front::LevelEditor::Layout::OnSceneLoaded(name, isNet);
    return true;
}

} // namespace Nx

REPLACE(0x00445130, Nx::CFunc_LoadScene);
