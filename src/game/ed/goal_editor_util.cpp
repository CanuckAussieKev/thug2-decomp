// The goal editor's small helpers: see goal_editor_util.h. From the disassembly of 0x004E6600-0x004E6B6E.

#include "game/ed/goal_editor_util.h"

#include <cstdint>
#include <cstdio>

#include "game/core/crc.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Ed {

namespace {

constexpr uint32_t kSkate = 0x54166ACD, kCombo = 0x4EC3CFB5, kCone = 0x25904450;

// The game's sprintf (its C runtime; 0x006234FE).
template <typename... Args>
int Sprintf(char* out, const char* format, Args... args)
{
    return GameFunction<int(__cdecl*)(char*, const char*, ...)>(0x006234FE)(out, format, args...);
}

} // namespace

// 0x004E6600
const char* __fastcall GoalObjectModel(uint32_t type, int index)
{
    if (type == kCone)
        return "Mdl_Cone/mdl_cone.mdl";
    if (type == kCombo) {
        switch (index) {
        case 0: return "gameobjects\\combo\\goal_combo_c\\goal_combo_c.mdl";
        case 1:
        case 4: return "gameobjects\\combo\\goal_combo_o\\goal_combo_o.mdl";
        case 2: return "gameobjects\\combo\\goal_combo_m\\goal_combo_m.mdl";
        case 3: return "gameobjects\\combo\\goal_combo_b\\goal_combo_b.mdl";
        default: return nullptr;
        }
    }
    if (type == kSkate) {
        switch (index) {
        case 0: return "gameobjects\\skate\\letter_s\\letter_s.mdl";
        case 1: return "gameobjects\\skate\\letter_k\\letter_k.mdl";
        case 2: return "gameobjects\\skate\\letter_a\\letter_a.mdl";
        case 3: return "gameobjects\\skate\\letter_t\\letter_t.mdl";
        case 4: return "gameobjects\\skate\\letter_e\\letter_e.mdl";
        default: return nullptr;
        }
    }
    return nullptr;
}

// 0x004E66A0
uint32_t __fastcall GoalObjectName(uint32_t type, int index)
{
    if (type == kCombo) {
        switch (index) {
        case 0: return 0x7BF1BA66;
        case 1:
        case 4: return 0x7247F64D;
        case 2: return 0x9C499761;
        case 3: return 0x0CF68AF0;
        default: return 0;
        }
    }
    if (type == kSkate) {
        switch (index) {
        case 0: return 0xF75425EA;
        case 1: return 0xE438BDBC;
        case 2: return 0x04ED54A2;
        case 3: return 0x6930B049;
        case 4: return 0x038090BB;
        default: return 0;
        }
    }
    return 0;
}

// 0x004E6730
uint32_t __fastcall EditedGoalNodeName(int which, int goal)
{
    char name[100];
    name[0] = '\0';
    if (which == 0)
        Sprintf(name, "TRG_EditedGoal%d_Pro", goal);
    else if (which == 1)
        Sprintf(name, "TRG_EditedGoal%d_Restart", goal);
    else
        Sprintf(name, "TRG_EditedGoal%d_Object_%d", goal, which - 2);
    return Crc::GenerateCRCFromString(name);
}

// 0x004E67A0
Script::CStruct* __fastcall GoalTypeStructure(uint32_t type)
{
    switch (type) {
    case 0x6FE44C6D: return Script::GetStructure(0xEF8F345F);
    case kCombo: return Script::GetStructure(0xB4F5801D);
    case 0x0FC6F698:
    case 0x7FE2238A:
    case 0xEA7BA666: return Script::GetStructure(0x675816B5);
    case kCone: return Script::GetStructure(0x34785A5B);
    case kSkate: return Script::GetStructure(0x3328976A);
    case 0x61C5D092: return Script::GetStructure(0x5756A675);
    case 0xD0DD047B: return Script::GetStructure(0xD3828793);
    case 0xEC414B76: return Script::GetStructure(0xB520A510);
    default: return nullptr;
    }
}

// 0x004E6890
Script::CStruct* GoalTypeDefaults(uint32_t type)
{
    switch (type) {
    case 0x6FE44C6D: return Script::GetStructure(0x28C7B799);
    case kCombo: return Script::GetStructure(0xFECBAB7C);
    case 0x0FC6F698: return Script::GetStructure(0x6626B0D8);
    case kCone: return Script::GetStructure(0xFD6CAF2A);
    case kSkate: return Script::GetStructure(0xE41E0E04);
    case 0x61C5D092: return Script::GetStructure(0x9005703A);
    case 0xEA7BA666: return Script::GetStructure(0xAD585D92);
    case 0x7FE2238A: return Script::GetStructure(0x38C1D87E);
    case 0xD0DD047B: {
        Script::CStruct* defaults = Script::GetStructure(0x97FEFF8F);
        Script::RunScriptOnce(0xFA54B475, nullptr, nullptr, defaults);
        return defaults;
    }
    case 0xEC414B76: return Script::GetStructure(0xAB62B082);
    default: return nullptr;
    }
}

// 0x004E69C0
int __cdecl GoalEditorConstant82()
{
    return 0x82;
}

// 0x004E69D0
void __fastcall InitEditedGoal(uint8_t* goal)
{
    auto u32 = [goal](uint32_t offset) -> uint32_t& { return *reinterpret_cast<uint32_t*>(goal + offset); };
    u32(0x2A4) = 5;
    u32(0x2B0) = 5;
    goal[0x28A] = 0;
    u32(0) = 0;
    goal[0x4] = 0;
    goal[0x4B] = 0;
    goal[0xAF] = 0;
    goal[0x36] = 0;
    u32(0x114) = 0;
    goal[0x286] = 0;
    goal[0x285] = 0;
    goal[0x284] = 0;
    goal[0x287] = 0;
    goal[0x288] = 0;
    goal[0x289] = 0;
    u32(0x298) = 100;
    u32(0x290) = 120;
    u32(0x29C) = 1;
    u32(0x2A0) = 0;
    u32(0x2A8) = 0x3DCCCCCD; // 0.1f
    u32(0x2AC) = 3000;
    u32(0x2B4) = 11;
    u32(0x2B8) = 360;
    goal[0x2BC] = 0;
    u32(0x2C0) = 2;
    u32(0x2C4) = 3;
    u32(0x2C8) = 8;
    u32(0x2F4) = 0;
    u32(0x294) = kSkate;
    goal[0x302] = 0;
    goal[0x303] = 0;
    for (uint32_t i = 0; i < 13; ++i) { // the thirteen 0x1C-byte entries from +0x118
        u32(0x118 + 0x1C * i) = 0;
        u32(0x12C + 0x1C * i) = 0;
    }
    u32(0x28C) = 0;
}

// 0x004E6AE0
void __stdcall RunMarkerObjectScript(int index)
{
    Script::CStruct params;
    params.Init();
    char* name = &GameVariable<char>(0x007CCC58);
    Sprintf(name, "GoalEditorMarkerObject%d", index);
    params.AddChecksum(0xA1DC81F9, Crc::GenerateCRCFromString(name));
    Script::RunScript(0xDCF3A25E, &params, nullptr, false, nullptr);
    params.Clear();
}

namespace {

// 0x004E6890: the type in EAX; EAX back (ECX and EDX as the call leaves them).
__declspec(naked) void GoalTypeDefaultsStub()
{
    __asm {
        push eax
        call GoalTypeDefaults
        add esp, 4
        ret
    }
}

// 0x004E69D0: its callers keep using ECX (the goal) after the call: kept (EAX 0 as the original leaves it).
__declspec(naked) void InitEditedGoalStub()
{
    __asm {
        push ecx
        call InitEditedGoal
        pop ecx
        xor eax, eax
        ret
    }
}

} // namespace

} // namespace Ed

REPLACE(0x004E6600, Ed::GoalObjectModel);
REPLACE(0x004E66A0, Ed::GoalObjectName);
REPLACE(0x004E6730, Ed::EditedGoalNodeName);
REPLACE(0x004E67A0, Ed::GoalTypeStructure);
REPLACE_UNCHECKED(0x004E6890, Ed::GoalTypeDefaultsStub);
REPLACE(0x004E69C0, Ed::GoalEditorConstant82);
REPLACE_UNCHECKED(0x004E69D0, Ed::InitEditedGoalStub);
REPLACE(0x004E6AE0, Ed::RunMarkerObjectScript);
