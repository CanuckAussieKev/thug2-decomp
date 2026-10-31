// Script commands preloading what the level's nodes and the pedestrians use (see preload_cfuncs.h).

#include "game/skate/preload_cfuncs.h"

#include <cstdint>
#include <cstring>
#include <new>

#include "game/assets/resource_registry.h"
#include "game/core/checksums.h"
#include "game/core/mem.h"
#include "game/core/str.h"
#include "game/core/text_store.h"
#include "game/maths/random.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/compositemgr.h"
#include "game/obj/entity.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/param_struct.h"
#include "game/skate/profile_cfuncs.h"
#include "game/core/code_address.h"
#include "game/skate/skate_module_calls.h"
#include "game/gfx/engine.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/model.h"
#include "game/gfx/model_appearance.h"
#include "game/gfx/model_builder.h"
#include "game/gfx/model_utils.h"
#include "game/skate/cas_options.h"

namespace ScriptCommands {

namespace {

using namespace SkateCalls;

constexpr uint32_t kClass = 0x12B4E660;           // Class
constexpr uint32_t kType = 0x7321A8D6;            // type
constexpr uint32_t kVehicle = 0xE47F1B79;         // vehicle
constexpr uint32_t kGameObject = 0xEF59C100;      // GameObject
constexpr uint32_t kPedestrian = 0xA0DFAC98;      // Pedestrian
constexpr uint32_t kModel = 0x286A8D26;           // model
constexpr uint32_t kSecondModel = 0x545F8172;     // (name unknown) a pedestrian's second model
constexpr uint32_t kProfile = 0x7EA855F0;         // profile
constexpr uint32_t kAllowReplaceTex = 0x6C37FDC7; // AllowReplaceTex
constexpr uint32_t kNoCollision = 0x0BF29BC0;     // nocollision
constexpr uint32_t kNone = 0x806FFF30;            // none
constexpr uint32_t kNoRandom = 0xF6F8E158;        // no_random
constexpr uint32_t kNoPedestrians = 0x2CAA92ED;   // (name unknown) a global integer: no pedestrians preloaded
constexpr uint32_t kPreloadModelFromAppearance = 0x53E82A70; // preload_model_from_appearance
constexpr uint32_t kPart = 0xB6F08F39;            // part
constexpr uint32_t kDescId = 0x4BB2084E;          // desc_id
constexpr uint32_t kPickPermitted = 0x355F9467;   // allowed_to_pick
constexpr uint32_t kAllowRandom = 0xF1E3CD22;     // allow_random
constexpr uint32_t kAlreadySelected = 0x92BDDFD9; // already_selected
constexpr uint32_t kShuffleIndex = 0x4B833E64;     // random_index
constexpr uint32_t kLevelSpecific = 0x3598BF7D;   // level_specific
constexpr uint32_t kExcludeFromLevels = 0x94D12F97; // exclude_from_levels
constexpr uint32_t kCasArtist = 0x2A648514;       // cas_artist
constexpr uint32_t kNum = 0x23BC5091;             // num
constexpr uint32_t kLight = 0x94E5A308, kTan = 0x85AAF0D8, kDark = 0xE4834204; // light, tan, dark
constexpr uint32_t kCarType = 0x5C0312EB;         // OBJECT_TYPE_CAR
constexpr uint32_t kObjReplaceTexture = 0x83F9BE15; // Obj_ReplaceTexture
// The capture-the-flag nodes' types (not preloaded in a game of one player).
constexpr uint32_t kFlagTypes[] = {0xBEBB41F0, 0x836284A5, 0xC3EBE05E, 0x4F8FF239,  // Flag_Red(_Base), Flag_Green(_Base)
                                   0x8CCA938A, 0x041E5BCF, 0xC2AF1EB1, 0xED3AD7FE}; // Flag_Blue(_Base), Flag_Yellow(_Base)
// The pedestrians' CAS part lists preloaded (unless no_random).
constexpr uint32_t kPedPartLists[] = {0xF56A8CFB, 0x26D93E89, 0xAF8139ED, 0xE5DC27BA,  // ped_m_head, torso, legs,
                                      0x9FAD7C38, 0x24DE6A73, 0xC546C92E, 0x02159819}; // accessories; ped_f_...

// Nx (not rewritten yet): a new model (0x0048BFA0, cdecl) and its deletion (0x0048C3C0); a model loaded from a file
// (0x004935E0, eight arguments); its geom count (0x00492E40) and a geom's texture dictionary (0x004946C0).
void* SpawnRenderModel(int) { return Engine3D::SpawnModel(); } // (0x0048BFA0 jumps to 0x004B29C0; its argument isn't used)
void DeleteModel(void* model) { Engine3D::Engine::DeleteGeomObject(model); }
void IngestMesh(void* model, const char* file, int a, int b, int c, uint32_t d, int e, int f, int g)
{
    static_cast<Engine3D::CRenderModel*>(model)->AddFileGeom(file, static_cast<uint32_t>(a), b != 0, c, d != 0, e != 0, f != 0, g != 0);
}
// 0x004879D0 (cdecl): the model file's path for `name` with `extension` (a String made in `out`, returned).
TextUtil::Chars* ModelPath(void* out, const char* name, const char* extension)
{
    return Gfx2D::MakeModelPath(static_cast<TextUtil::Chars*>(out), name, extension);
}
// 0x0048BFE0 (cdecl): a model file's base name (without its extension) into `base`.
void BaseName(const char* name, char* base) { Engine3D::Engine::SplitSkinName(name, base); }

// 0x00565BB0: the skate module's: whether a node isn't for this game (its own 0x68910AC6 test).
bool SkipNode(Scripts::CStruct* node)
{
    return GameModules::Skate::ShouldBeAbsent(node) != 0;
}

// 0x005AAAA0 (gamenet-2's range: the list's name in EAX): every part of a global CAS part list with allow_random
// preloaded (its mesh kept in memory).
void PreloadPartList(uint32_t list)
{
    void* stub = CodeAddress(ScriptCommands::PreloadMeshesStub);
    __asm {
        mov eax, list
        mov ecx, stub
        call ecx
    }
}

// The CAS option lists (no source yet): 0x00485CF0 the option `descriptorId` of a slot's global list; 0x00485D70 the
// options of a slot's list matching a filter (0: any of them allowed to pick) into `out`, their count in `count`.
Scripts::CStruct* CustomiseChoice(uint32_t part, uint32_t descriptorId, bool assert)
{
    return GameObjects::FindCasOption(part, descriptorId, assert);
}
void MatchingOptions(uint32_t part, uint32_t filter, Scripts::CStruct** out, int* count)
{
    GameObjects::GetMatchingCasOptions(part, filter, out, count);
}

// The appearance a pedestrian profile describes (Gfx2D::CModelLook, 0x5C bytes: constructor 0x00488EA0,
// SetAppearance 0x00488F40, destructor body 0x004890F0) and the model builder (constructor 0x004897D0 with (1, 8),
// its build 0x0048A2F0, destroyed as the CEntity it is - 0x004615D0).
struct Appearance {
    alignas(4) uint8_t m_bytes[0x5C];
};
struct ModelAssembler {
    alignas(4) uint8_t m_bytes[0x54];
};

Scripts::CStruct* Element(Scripts::CArray* array, uint32_t index)
{
    return reinterpret_cast<Scripts::CStruct*>(array->FetchPart(index));
}
// A pedestrian part chosen at random: allowed, not to be picked again, its place among the random choices.
void Choose(Scripts::CStruct* option, int index)
{
    option->PutInt(kAllowRandom, 1);
    option->PutInt(kPickPermitted, 0);
    option->PutInt(kShuffleIndex, index);
}

} // namespace

// 0x005AAFA0: PreloadModels - every vehicle and game object node's model (not "none"; a flag's only in a game of
// more than one player) loaded and dropped, keeping its data: AllowReplaceTex passed on; unless nocollision, its
// .col file too; a vehicle's .qb too.
bool __cdecl CFunc_PreloadModels(Scripts::CStruct*, Scripts::CScript*)
{
    MemSys::PushMemProfile("PreLoadModels");
    LevelNodes::StartNodeWalk(0);
    for (Scripts::CStruct* node = LevelNodes::NextNode(); node; node = LevelNodes::NextNode()) {
        if (SkipNode(node))
            continue;
        uint32_t nodeClass = 0, type = 0;
        node->ReadHashValue(kClass, &nodeClass, 0);
        node->ReadHashValue(kType, &type, 0);
        if (!IsMultiplayer()) {
            bool flag = false;
            for (uint32_t flagType : kFlagTypes)
                flag = flag || type == flagType;
            if (flag)
                continue;
        }
        if (nodeClass != kVehicle && nodeClass != kGameObject)
            continue;
        const char* model = nullptr;
        if (!node->FetchText(kModel, &model, 0))
            continue;
        node->FetchText(kModel, &model, 1);
        if (_stricmp("none", model) == 0)
            continue;
        TextUtil::Chars path;
        {
            alignas(4) uint8_t storage[sizeof(TextUtil::Chars)];
            TextUtil::Chars* made = ModelPath(storage, model, ".mdl");
            path = *made;
            made->~Chars();
        }
        void* nxModel = SpawnRenderModel(1);
        IngestMesh(nxModel, path.c_str(), 0, 1, 0, node->HasBareFlag(kAllowReplaceTex), 0, 1, 0);
        if (!node->HasBareFlag(kNoCollision)) {
            const char* file = path.c_str();
            char name[0x100];
            std::memcpy(name, file, std::strlen(file) + 1); // (no bound: as the original)
            TextUtil::ToLowerText(name);
            if (std::strstr(name, ".mdl")) {
                BaseName(file, name);
                std::memcpy(name + std::strlen(name), ".col", 5);
                AssetStore::CResourceRegistry* assets = AssetStore::AssetManager();
                bool permanent = assets->GetPermanentByDefault();
                assets->FetchOrLoadResource(name, 8, 0, Checksums::HashFromText(file), false, false, permanent, nullptr, 0);
            }
        }
        if (nodeClass == kVehicle) {
            char script[0x100];
            BaseName(path.c_str(), script);
            std::memcpy(script + std::strlen(script), ".qb", 4);
            AssetStore::CResourceRegistry* assets = AssetStore::AssetManager();
            bool permanent = assets->GetPermanentByDefault();
            assets->FetchOrLoadResource(script, 0xA, 0, 0, false, false, permanent, nullptr, 0);
        }
        DeleteModel(nxModel);
    }
    LevelNodes::EndNodeWalk();
    MemSys::PopMemProfile();
    return true;
}

// 0x005AB300: PreloadPedestrians [no_random] - not in a network game, nor with the global 0x2CAA92ED set (false):
// the pedestrians' CAS part lists (unless no_random), every pedestrian node's .skin and second (.mdl) models, and the
// model its profile's appearance builds (preload_model_from_appearance) loaded and dropped.
bool __cdecl CFunc_PreloadPedestrians(Scripts::CStruct* params, Scripts::CScript*)
{
    if (OnlinePlay::g_onlineHub->InNetGame())
        return false;
    if (Scripts::FetchIntValue(kNoPedestrians))
        return false;
    MemSys::PushMemProfile("Random Ped Parts");
    if (!params->HasBareFlag(kNoRandom)) {
        for (uint32_t list : kPedPartLists)
            PreloadPartList(list);
    }
    MemSys::PopMemProfile();
    MemSys::PushMemProfile("Unique Ped Parts");
    LevelNodes::StartNodeWalk(0);
    for (Scripts::CStruct* node = LevelNodes::NextNode(); node; node = LevelNodes::NextNode()) {
        uint32_t nodeClass = 0;
        node->ReadHashValue(kClass, &nodeClass, 0);
        if (nodeClass != kPedestrian)
            continue;
        const char* model;
        if (node->FetchText(kModel, &model, 0) && Checksums::HashFromText(model) != kNone) {
            TextUtil::Chars path;
            alignas(4) uint8_t storage[sizeof(TextUtil::Chars)];
            TextUtil::Chars* made = ModelPath(storage, model, ".skin");
            path = *made;
            made->~Chars();
            void* nxModel = SpawnRenderModel(1);
            IngestMesh(nxModel, path.c_str(), 0, 1, 8, 0, 0, 1, 0);
            DeleteModel(nxModel);
        }
        if (node->FetchText(kSecondModel, &model, 0)) {
            TextUtil::Chars path;
            alignas(4) uint8_t storage[sizeof(TextUtil::Chars)];
            TextUtil::Chars* made = ModelPath(storage, model, ".mdl");
            path = *made;
            made->~Chars();
            void* nxModel = SpawnRenderModel(1);
            IngestMesh(nxModel, path.c_str(), 0, 1, 8, 0, 0, 1, 0);
            DeleteModel(nxModel);
        }
        uint32_t profileName;
        if (!node->ReadHashValue(kProfile, &profileName, 0))
            continue;
        Scripts::CStruct* profile = Scripts::ReadSubStruct(profileName);
        if (!profile)
            continue;
        void* nxModel = SpawnRenderModel(1);
        Appearance appearance;
        auto* gfxAppearance = reinterpret_cast<Gfx2D::CModelLook*>(&appearance);
        gfxAppearance->MakeNew();
        gfxAppearance->ReadIn(profile, false);
        ModelAssembler builder;
        auto* gfxBuilder = reinterpret_cast<Gfx2D::CModelAssembler*>(&builder);
        gfxBuilder->MakeNew(true, 8);
        gfxBuilder->Build(gfxAppearance, static_cast<Engine3D::CRenderModel*>(nxModel), nullptr, kPreloadModelFromAppearance);
        DeleteModel(nxModel);
        reinterpret_cast<GameObjects::CEntity*>(&builder)->DestroyBody();
        gfxAppearance->DestroyBody();
    }
    LevelNodes::EndNodeWalk();
    MemSys::PopMemProfile();
    return true;
}

// 0x005AB690: PreselectRandomPedestrians part=<slot> [num=<n>] - the slot's CAS options marked for the random
// pedestrians: those allowed here (not no_random; level_specific to this level, or not excluded from it) are
// candidates; the pedestrian nodes' own parts are taken; then one option of any kind (failing that one light, one
// tan and one dark) and the rest at random among the candidates, n (the candidates) in all, numbered by
// random_index. (No candidate left while choosing: the original loops for ever - kept.)
bool __cdecl CFunc_PreselectRandomPedestrians(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t part = 0; // (uninitialised in the original when missing)
    params->ReadHashValue(kPart, &part, 1);
    Scripts::CArray* list = Scripts::FetchList(part);
    uint32_t size = list->m_extent;
    uint32_t level = SkateField<uint32_t>(0x374); // (the level the parts are chosen for)
    for (uint32_t i = 0; i < size; ++i) {
        Scripts::CStruct* option = Element(list, i);
        option->EraseModule(kPickPermitted);
        option->PutInt(kAllowRandom, 0);
        option->PutInt(kAlreadySelected, 0);
        option->PutInt(kShuffleIndex, -1);
    }
    int candidates = 0;
    for (uint32_t i = 0; i < size; ++i) {
        Scripts::CStruct* option = Element(list, i);
        Scripts::CStruct* levels;
        bool candidate;
        if (option->HasEntryNamed(kNoRandom)) {
            if (option->ReadSubStruct(kLevelSpecific, &levels, 0))
                Scripts::DumpToLog(option);
            candidate = false;
        } else if (option->ReadSubStruct(kLevelSpecific, &levels, 0)) {
            candidate = levels->HasEntryNamed(level);
        } else if (option->ReadSubStruct(kExcludeFromLevels, &levels, 0)) {
            candidate = !levels->HasEntryNamed(level);
        } else {
            candidate = true;
        }
        if (candidate) {
            ++candidates;
            option->PutInt(kPickPermitted, 1);
        } else {
            option->PutInt(kPickPermitted, 0);
            Scripts::FetchIntValue(kCasArtist); // (read and not used)
        }
    }
    LevelNodes::StartNodeWalk(0);
    for (Scripts::CStruct* node = LevelNodes::NextNode(); node; node = LevelNodes::NextNode()) {
        uint32_t nodeClass = 0;
        node->ReadHashValue(kClass, &nodeClass, 0);
        if (nodeClass != kPedestrian)
            continue;
        uint32_t profileName;
        if (!node->ReadHashValue(kProfile, &profileName, 0))
            continue;
        Scripts::CStruct* profile = Scripts::ReadSubStruct(profileName);
        if (!profile)
            continue;
        Scripts::CStruct* slot;
        if (!profile->ReadSubStruct(part, &slot, 0))
            continue;
        uint32_t descriptorId;
        if (!slot->ReadHashValue(kDescId, &descriptorId, 0))
            continue;
        Scripts::CStruct* option = CustomiseChoice(part, descriptorId, false);
        if (!option || option->HasEntryNamed(kNoRandom))
            continue;
        Scripts::FetchIntValue(kCasArtist); // (read and not used)
        option->PutInt(kAllowRandom, 1);
        option->PutInt(kPickPermitted, 1);
        option->PutInt(kAlreadySelected, 1);
    }
    LevelNodes::EndNodeWalk();
    int wanted = candidates;
    params->FetchIntValue(kNum, &wanted, 0);
    if (wanted > candidates)
        wanted = candidates;
    Scripts::CStruct* matching[64];
    int count = 0;
    MatchingOptions(part, 0, matching, &count);
    if (count) {
        Choose(matching[Maths::Rnd(count)], wanted - 1);
    } else {
        MatchingOptions(part, kLight, matching, &count);
        Choose(matching[Maths::Rnd(count)], wanted - 1);
        --wanted;
        MatchingOptions(part, kTan, matching, &count);
        Choose(matching[Maths::Rnd(count)], wanted - 1);
        --wanted;
        MatchingOptions(part, kDark, matching, &count);
        Choose(matching[Maths::Rnd(count)], wanted - 1);
    }
    --wanted;
    for (int n = 0; n < wanted; ++n) {
        int allowed = 0;
        Scripts::CStruct* option;
        do {
            option = Element(list, static_cast<uint32_t>(Maths::Rnd(static_cast<int>(size))));
            option->FetchIntValue(kPickPermitted, &allowed, 1);
        } while (!allowed);
        Choose(option, n);
    }
    return true;
}

// 0x005ABB20: ReplaceCarTextures ... - every car's model component whose geoms use a texture dictionary not met yet
// (in this walk) runs Obj_ReplaceTexture with the parameters.
bool __cdecl CFunc_ReplaceCarTextures(Scripts::CStruct* params, Scripts::CScript* script)
{
    auto* objects = GameObjects::g_compositeRegistry;
    // (the original keeps 32 on its stack without a bound: more would overwrite its frame; here the rest aren't kept)
    uint32_t seen[32];
    int seenCount = 0;
    for (GameObjects::CObjectModule* component = objects->FindFirstModuleOfKind(kModel); component;
         component = component->mp_nextOfType) {
        if (component->m_entity->m_kindTag != kCarType)
            continue;
        void* model = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(component) + 0x208);
        bool added = false;
        for (int i = 0; i < static_cast<Engine3D::CRenderModel*>(model)->FetchGeomTotal(); ++i) {
            uint32_t textures =
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<Engine3D::CRenderModel*>(model)->GetGeomTextures(i)));
            bool found = false;
            for (int k = 0; k < seenCount; ++k) {
                if (seen[k] == textures)
                    found = true;
            }
            if (!found && seenCount < 32) {
                seen[seenCount++] = textures;
                added = true;
            }
        }
        if (added)
            component->InvokeMethodByName(kObjReplaceTexture, params, script);
    }
    return true;
}

} // namespace ScriptCommands
