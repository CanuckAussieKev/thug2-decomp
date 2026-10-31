// Script commands (CFunctions) and helpers 0x005AA930-0x005AAEEF: profiles and the career's menu numbers (see
// profile_cfuncs.h).

#include "game/skate/profile_cfuncs.h"

#include <cstdint>
#include <cstdio>

#include "engine/core/manager.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/career.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module.h"
#include "game/park_editing/goal_editor.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/model.h"
#include "engine/gfx/model_appearance.h"

namespace ScriptCommands {

SwapSlot g_swapTable[5];

namespace {

using Scripts::CScript;
using Scripts::CStruct;

constexpr int kSwapSlots = 5;
SwapSlot* SwapTable() { return g_swapTable; }

GameObjects::CProfileRoster* Profiles() { return GameObjects::g_profileRoster; }
GameObjects::CRiderStoryProgress* Career()
{
    return *reinterpret_cast<GameObjects::CRiderStoryProgress**>(reinterpret_cast<uint8_t*>(GameModules::Skate::Sole()) + 0x20);
}
CStruct* ResultParams(CScript* script) { return *reinterpret_cast<CStruct**>(reinterpret_cast<uint8_t*>(script) + 0x14); }

// The skate module's profile `params`' skater names (0x00564F80).
GameObjects::CRiderIdentity* SkateProfile(CStruct* params) { return GameModules::Skate::Sole()->FetchAccount(params); }

const char* const kNoText = "---"; // "---"

} // namespace

// 0x005AA930
void __cdecl SwapMapping(uint32_t a, uint32_t b)
{
    SwapSlot* table = SwapTable();
    int slotA = -1;
    int slotB = -1;
    for (int i = 0; i < kSwapSlots; ++i) {
        if (!table[i].used)
            continue;
        if (table[i].key == a)
            slotA = i;
        else if (table[i].key == b)
            slotB = i;
    }
    if (slotA == -1 || slotB == -1) {
        for (int i = 0; i < kSwapSlots; ++i) {
            if (table[i].used)
                continue;
            if (slotA == -1) {
                slotA = i;
                table[i].key = a;
                table[i].value = a;
            } else if (slotB == -1) {
                slotB = i;
                table[i].key = b;
                table[i].value = b;
            } else {
                continue;
            }
            table[i].used = true;
        }
    }
    // (with the table full, a slot of -1: the entry before the table, as in the original)
    SwapSlot& first = table[slotA];
    SwapSlot& second = table[slotB];
    uint32_t firstValue = first.value;
    uint32_t secondValue = second.value;
    if (first.key == secondValue)
        first.used = false;
    else
        first.value = secondValue;
    if (second.key == firstValue)
        second.used = false;
    else
        second.value = firstValue;
}

// 0x005AAAA0
void PreloadMeshes(uint32_t arrayName)
{
    Scripts::CArray* list = Scripts::FetchList(arrayName);
    for (uint32_t i = 0; i < list->m_extent; ++i) {
        auto* entry = reinterpret_cast<CStruct*>(list->FetchPart(i));
        int allowRandom = 0;
        entry->FetchIntValue(0xF1E3CD22u, &allowRandom, 0); // allow_random
        if (!allowRandom)
            continue;
        const char* mesh;
        if (!entry->FetchText(0x1E90C5A9u, &mesh, 0)) // mesh
            continue;
        // a model made (0x0048BFA0), the mesh added to it (0x004935E0), the model deleted (0x0048C3C0)
        void* model = Engine3D::SpawnModel();
        static_cast<Engine3D::CRenderModel*>(model)->AddFileGeom(mesh, 0, true, 0, false, false, true, false);
        Engine3D::Engine::DeleteGeomObject(model);
    }
}

namespace {
void __cdecl PreloadMeshesRegisters(uint32_t arrayName) { PreloadMeshes(arrayName); }
} // namespace

// 0x005AAAA0: the array's name in EAX.
__declspec(naked) void PreloadMeshesStub()
{
    __asm {
        push eax
        call PreloadMeshesRegisters
        add esp, 4
        ret
    }
}

// 0x005AAA80
bool __cdecl CFunc_AddGoalEditorNodesToLevelNodeArray(CStruct*, CScript*)
{
    // the goal editor's nodes added (0x004E8F50, no source yet)
    ParkEditing::AddGoalEditorNodes(nullptr, nullptr, MemSys::Hub::Pull()->m_bottomUpHeap);
    return true;
}

// 0x005AAB50
bool __cdecl CFunc_CurrentSkaterIsPro(CStruct* params, CScript*)
{
    GameObjects::CRiderIdentity* profile = SkateProfile(params);
    return profile && profile->IsPro();
}

// 0x005AAB80
bool __cdecl CFunc_GetGoalsCompleted(CStruct* params, CScript* script)
{
    int level = 0;
    params->FetchIntValue(0u, &level, 0);
    int goals = Career()->CountGoals(level);
    char text[100];
    std::snprintf(text, sizeof(text), "%d/9", goals);
    ResultParams(script)->AppendWord(0xC661FE79u, Scripts::QBVAL_STRING, text);  // (name unknown)
    ResultParams(script)->AppendWord(0xA43DC969u, Scripts::QBVAL_STRING, kNoText); // (name unknown)
    return true;
}

// 0x005AAC10
bool __cdecl CFunc_GetNextLevelRequirements(CStruct*, CScript* script)
{
    char text[100];
    std::snprintf(text, sizeof(text), "%s", kNoText);
    GameObjects::CRiderStoryProgress* career = Career();
    int goals = career->CountTotalGoals();
    int medals = career->CountLevelsWithGoals();
    Scripts::CArray* steps = Scripts::FetchList(0x1D8F1A7Eu); // (name unknown) the level steps
    for (uint32_t i = 0; i < steps->m_extent; ++i) {
        auto* step = reinterpret_cast<CStruct*>(steps->FetchPart(i));
        int goalsNeeded = 0;
        int medalsNeeded = 0;
        if (step->HasBareFlag(0x38DBE1D0u) || step->HasBareFlag(0x032314D1u)) // goals / goal
            step->FetchIntValue(0u, &goalsNeeded, 0);
        if (step->HasBareFlag(0x56907F8Bu) || step->HasBareFlag(0x23BBA846u)) // (names unknown) medals
            step->FetchIntValue(0u, &medalsNeeded, 0);
        if (goals < goalsNeeded) {
            std::snprintf(text, sizeof(text), Scripts::FetchText(0x407BFD24u), goalsNeeded - goals); // cfuncs_str_goalsfornextlevel
            break;
        }
        if (medals < medalsNeeded) {
            std::snprintf(text, sizeof(text), Scripts::FetchText(0x4F3E243Eu), medalsNeeded - medals); // cfuncs_str_medalsfornextlevel
            break;
        }
    }
    ResultParams(script)->AppendWord(0xAF2305A6u, Scripts::QBVAL_STRING, text); // (name unknown)
    return true;
}

// 0x005AAD70
bool __cdecl CFunc_SetCurrentSkaterProfile(CStruct* params, CScript*)
{
    int index = 0;
    params->FetchIntValue(0u, &index, 0);
    Profiles()->SetCurrentIndex(index);
    return true;
}

// 0x005AADA0
bool __cdecl CFunc_AddSkaterProfile(CStruct* params, CScript*) { return Profiles()->AddTemplate(params); }

// 0x005AADC0
bool __cdecl CFunc_AddTemporaryProfile(CStruct* params, CScript*)
{
    uint32_t name;
    params->ReadHashValue(0xA1DC81F9u, &name, 1); // name (asserted: used as it is)
    return Profiles()->AddExtraProfile(name);
}

// 0x005AADF0
bool __cdecl CFunc_RememberTemporaryAppearance(CStruct* params, CScript*)
{
    uint32_t name;
    params->ReadHashValue(0xA1DC81F9u, &name, 1); // name
    // a model appearance (Gfx2D::CModelLook, 0x5C bytes: constructor 0x00488EA0, destructor body 0x004890F0)
    alignas(4) uint8_t appearance[0x5C];
    reinterpret_cast<Gfx2D::CModelLook*>(appearance)->MakeNew();
    GameObjects::CProfileRoster* profiles = Profiles();
    uint8_t* source;
    CStruct* structure = nullptr;
    if (params->ReadSubStruct(0xEFC8944Cu, &structure, 0)) { // appearance_structure
        reinterpret_cast<Gfx2D::CModelLook*>(appearance)->ReadIn(structure, true);
        source = appearance;
    } else {
        int player; // (asserted: used as it is)
        params->FetchIntValue(0x67E6859Au, &player, 1); // player
        source = reinterpret_cast<uint8_t*>(profiles->FetchAccount(player));
    }
    auto* temporary = reinterpret_cast<uint8_t*>(profiles->GetExtraProfile(name));
    if (params->HasBareFlag(0x3AF36E6Du)) { // NoFaceTexture: only the appearance's structure (+0x4C)
        auto* own = reinterpret_cast<CStruct*>(temporary + 0x4C);
        own->Clear();
        own->AppendStructureIfAny(reinterpret_cast<CStruct*>(source + 0x4C));
    } else {
        reinterpret_cast<Gfx2D::CModelLook*>(temporary)->CopyFrom(
            reinterpret_cast<Gfx2D::CModelLook*>(source)); // the appearance copied
    }
    reinterpret_cast<Gfx2D::CModelLook*>(appearance)->DestroyBody();
    return true;
}

} // namespace ScriptCommands
