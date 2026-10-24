// Script commands (CFunctions) and helpers 0x005AA930-0x005AAEEF: profiles and the career's menu numbers (see
// profile_cfuncs.h).

#include "game/skate/profile_cfuncs.h"

#include <cstdint>
#include <cstdio>

#include "game/core/manager.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace CFuncs {

namespace {

using Script::CScript;
using Script::CStruct;

// The swap table (0x007CEDE0): five slots.
struct SwapSlot {
    bool used;
    uint8_t pad[3];
    uint32_t key;
    uint32_t value;
};
static_assert(sizeof(SwapSlot) == 12, "layout must match the original");
constexpr int kSwapSlots = 5;
SwapSlot* SwapTable() { return &GameVariable<SwapSlot>(0x007CEDE0); }

Obj::CPlayerProfileManager* Profiles() { return GameVariable<Obj::CPlayerProfileManager*>(0x007CE4B4); }
Obj::CSkaterCareer* Career()
{
    return *reinterpret_cast<Obj::CSkaterCareer**>(reinterpret_cast<uint8_t*>(Mdl::Skate::Instance()) + 0x20);
}
CStruct* ResultParams(CScript* script) { return *reinterpret_cast<CStruct**>(reinterpret_cast<uint8_t*>(script) + 0x14); }

// The skate module's profile `index` (0x00564F80, no source yet).
Obj::CSkaterProfile* SkateProfile(int index)
{
    return GameFunction<Obj::CSkaterProfile*(__thiscall*)(Mdl::Skate*, int)>(0x00564F80)(Mdl::Skate::Instance(), index);
}

const char* const kNoText = reinterpret_cast<const char*>(0x0064F7B4); // "---"

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
    Script::CArray* list = Script::GetArray(arrayName);
    for (uint32_t i = 0; i < list->m_size; ++i) {
        auto* entry = reinterpret_cast<CStruct*>(list->GetElement(i));
        int allowRandom = 0;
        entry->GetInteger(0xF1E3CD22u, &allowRandom, 0); // allow_random
        if (!allowRandom)
            continue;
        const char* mesh;
        if (!entry->GetString(0x1E90C5A9u, &mesh, 0)) // mesh
            continue;
        // a model made (0x0048BFA0), the mesh added to it (0x004935E0), the model deleted (0x0048C3C0)
        void* model = GameFunction<void*(__cdecl*)(int)>(0x0048BFA0)(1);
        GameFunction<void*(__thiscall*)(void*, const char*, int, int, int, int, int, int, int)>(0x004935E0)(
            model, mesh, 0, 1, 0, 0, 0, 1, 0);
        GameFunction<void(__cdecl*)(void*)>(0x0048C3C0)(model);
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
    GameFunction<void(__cdecl*)(int, int, void*)>(0x004E8F50)(0, 0, Mem::Manager::Get()->m_bottomUpHeap);
    return true;
}

// 0x005AAB50
bool __cdecl CFunc_CurrentSkaterIsPro(CStruct* params, CScript*)
{
    Obj::CSkaterProfile* profile = SkateProfile(static_cast<int>(reinterpret_cast<intptr_t>(params)));
    return profile && profile->IsPro();
}

// 0x005AAB80
bool __cdecl CFunc_GetGoalsCompleted(CStruct* params, CScript* script)
{
    int level = 0;
    params->GetInteger(0u, &level, 0);
    int goals = Career()->CountGoals(level);
    char text[100];
    std::snprintf(text, sizeof(text), "%d/9", goals);
    ResultParams(script)->AddString(0xC661FE79u, Script::ESYMBOLTYPE_STRING, text);  // (name unknown)
    ResultParams(script)->AddString(0xA43DC969u, Script::ESYMBOLTYPE_STRING, kNoText); // (name unknown)
    return true;
}

// 0x005AAC10
bool __cdecl CFunc_GetNextLevelRequirements(CStruct*, CScript* script)
{
    char text[100];
    std::snprintf(text, sizeof(text), "%s", kNoText);
    Obj::CSkaterCareer* career = Career();
    int goals = career->CountTotalGoals();
    int medals = career->CountLevelsWithGoals();
    Script::CArray* steps = Script::GetArray(0x1D8F1A7Eu); // (name unknown) the level steps
    for (uint32_t i = 0; i < steps->m_size; ++i) {
        auto* step = reinterpret_cast<CStruct*>(steps->GetElement(i));
        int goalsNeeded = 0;
        int medalsNeeded = 0;
        if (step->ContainsFlag(0x38DBE1D0u) || step->ContainsFlag(0x032314D1u)) // goals / goal
            step->GetInteger(0u, &goalsNeeded, 0);
        if (step->ContainsFlag(0x56907F8Bu) || step->ContainsFlag(0x23BBA846u)) // (names unknown) medals
            step->GetInteger(0u, &medalsNeeded, 0);
        if (goals < goalsNeeded) {
            std::snprintf(text, sizeof(text), Script::GetString(0x407BFD24u), goalsNeeded - goals); // cfuncs_str_goalsfornextlevel
            break;
        }
        if (medals < medalsNeeded) {
            std::snprintf(text, sizeof(text), Script::GetString(0x4F3E243Eu), medalsNeeded - medals); // cfuncs_str_medalsfornextlevel
            break;
        }
    }
    ResultParams(script)->AddString(0xAF2305A6u, Script::ESYMBOLTYPE_STRING, text); // (name unknown)
    return true;
}

// 0x005AAD70
bool __cdecl CFunc_SetCurrentSkaterProfile(CStruct* params, CScript*)
{
    int index = 0;
    params->GetInteger(0u, &index, 0);
    Profiles()->SetCurrentIndex(index);
    return true;
}

// 0x005AADA0
bool __cdecl CFunc_AddSkaterProfile(CStruct* params, CScript*) { return Profiles()->AddTemplate(params); }

// 0x005AADC0
bool __cdecl CFunc_AddTemporaryProfile(CStruct* params, CScript*)
{
    uint32_t name;
    params->GetChecksum(0xA1DC81F9u, &name, 1); // name (asserted: used as it is)
    return Profiles()->AddExtraProfile(name);
}

// 0x005AADF0
bool __cdecl CFunc_RememberTemporaryAppearance(CStruct* params, CScript*)
{
    uint32_t name;
    params->GetChecksum(0xA1DC81F9u, &name, 1); // name
    // a model appearance (Gfx::CModelAppearance, 0x5C bytes: constructor 0x00488EA0, destructor body 0x004890F0)
    alignas(4) uint8_t appearance[0x5C];
    GameFunction<void*(__thiscall*)(void*)>(0x00488EA0)(appearance);
    Obj::CPlayerProfileManager* profiles = Profiles();
    uint8_t* source;
    CStruct* structure = nullptr;
    if (params->GetStructure(0xEFC8944Cu, &structure, 0)) { // appearance_structure
        GameFunction<void(__thiscall*)(void*, CStruct*, bool)>(0x00488F40)(appearance, structure, true);
        source = appearance;
    } else {
        int player; // (asserted: used as it is)
        params->GetInteger(0x67E6859Au, &player, 1); // player
        source = reinterpret_cast<uint8_t*>(profiles->GetProfile(player));
    }
    auto* temporary = reinterpret_cast<uint8_t*>(profiles->GetExtraProfile(name));
    if (params->ContainsFlag(0x3AF36E6Du)) { // NoFaceTexture: only the appearance's structure (+0x4C)
        auto* own = reinterpret_cast<CStruct*>(temporary + 0x4C);
        own->Clear();
        own->AppendStructureIfAny(reinterpret_cast<CStruct*>(source + 0x4C));
    } else {
        GameFunction<void*(__thiscall*)(void*, const void*)>(0x004893A0)(temporary, source); // the appearance copied
    }
    GameFunction<void(__thiscall*)(void*)>(0x004890F0)(appearance);
    return true;
}

} // namespace CFuncs

REPLACE(0x005AA930, CFuncs::SwapMapping);
REPLACE(0x005AAA80, CFuncs::CFunc_AddGoalEditorNodesToLevelNodeArray);
REPLACE_UNCHECKED(0x005AAAA0, CFuncs::PreloadMeshesStub); // the array's name in EAX
REPLACE(0x005AAB50, CFuncs::CFunc_CurrentSkaterIsPro);
REPLACE(0x005AAB80, CFuncs::CFunc_GetGoalsCompleted);
REPLACE(0x005AAC10, CFuncs::CFunc_GetNextLevelRequirements);
REPLACE(0x005AAD70, CFuncs::CFunc_SetCurrentSkaterProfile);
REPLACE(0x005AADA0, CFuncs::CFunc_AddSkaterProfile);
REPLACE(0x005AADC0, CFuncs::CFunc_AddTemporaryProfile);
REPLACE(0x005AADF0, CFuncs::CFunc_RememberTemporaryAppearance);
