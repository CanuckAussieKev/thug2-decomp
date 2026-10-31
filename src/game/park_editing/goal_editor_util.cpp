// The goal editor's small helpers: see goal_editor_util.h. From the disassembly of 0x004E6600-0x004E6B6E.

#include "game/park_editing/goal_editor_util.h"

#include <cstdint>
#include <cstdio>

#include "engine/core/checksums.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/crt.h"

namespace park_editing {

namespace {

constexpr uint32_t kSkate = 0x54166ACD, kCombo = 0x4EC3CFB5, kCone = 0x25904450;

// The game's sprintf (its C runtime; 0x006234FE).
template <typename... Args>
int get_sprintf(char* out, const char* format, Args... args)
{
    return crt::get_sprintf(out, format, args...);
}

} // namespace

// 0x004E6600
const char* __fastcall goal_object_model(uint32_t type, int index)
{
    if (type == kCone)
        return "Mdl_Cone/mdl_cone.mdl";
    if (type == kCombo) {
        switch (index) {
        case 0:
            return "gameobjects\\combo\\goal_combo_c\\goal_combo_c.mdl";
        case 1:
        case 4:
            return "gameobjects\\combo\\goal_combo_o\\goal_combo_o.mdl";
        case 2:
            return "gameobjects\\combo\\goal_combo_m\\goal_combo_m.mdl";
        case 3:
            return "gameobjects\\combo\\goal_combo_b\\goal_combo_b.mdl";
        default:
            return nullptr;
        }
    }
    if (type == kSkate) {
        switch (index) {
        case 0:
            return "gameobjects\\skate\\letter_s\\letter_s.mdl";
        case 1:
            return "gameobjects\\skate\\letter_k\\letter_k.mdl";
        case 2:
            return "gameobjects\\skate\\letter_a\\letter_a.mdl";
        case 3:
            return "gameobjects\\skate\\letter_t\\letter_t.mdl";
        case 4:
            return "gameobjects\\skate\\letter_e\\letter_e.mdl";
        default:
            return nullptr;
        }
    }
    return nullptr;
}

// 0x004E66A0
uint32_t __fastcall goal_object_name(uint32_t type, int index)
{
    if (type == kCombo) {
        switch (index) {
        case 0:
            return 0x7BF1BA66;
        case 1:
        case 4:
            return 0x7247F64D;
        case 2:
            return 0x9C499761;
        case 3:
            return 0x0CF68AF0;
        default:
            return 0;
        }
    }
    if (type == kSkate) {
        switch (index) {
        case 0:
            return 0xF75425EA;
        case 1:
            return 0xE438BDBC;
        case 2:
            return 0x04ED54A2;
        case 3:
            return 0x6930B049;
        case 4:
            return 0x038090BB;
        default:
            return 0;
        }
    }
    return 0;
}

// 0x004E6730
uint32_t __fastcall edited_goal_node_name(int which, int goal)
{
    char name[100];
    name[0] = '\0';
    if (which == 0)
        get_sprintf(name, "TRG_EditedGoal%d_Pro", goal);
    else if (which == 1)
        get_sprintf(name, "TRG_EditedGoal%d_Restart", goal);
    else
        get_sprintf(name, "TRG_EditedGoal%d_Object_%d", goal, which - 2);
    return checksums::hash_from_text(name);
}

// 0x004E67A0
script::Struct* __fastcall goal_type_structure(uint32_t type)
{
    switch (type) {
    case 0x6FE44C6D:
        return script::read_sub_struct(0xEF8F345F);
    case kCombo:
        return script::read_sub_struct(0xB4F5801D);
    case 0x0FC6F698:
    case 0x7FE2238A:
    case 0xEA7BA666:
        return script::read_sub_struct(0x675816B5);
    case kCone:
        return script::read_sub_struct(0x34785A5B);
    case kSkate:
        return script::read_sub_struct(0x3328976A);
    case 0x61C5D092:
        return script::read_sub_struct(0x5756A675);
    case 0xD0DD047B:
        return script::read_sub_struct(0xD3828793);
    case 0xEC414B76:
        return script::read_sub_struct(0xB520A510);
    default:
        return nullptr;
    }
}

// 0x004E6890
script::Struct* goal_type_defaults(uint32_t type)
{
    switch (type) {
    case 0x6FE44C6D:
        return script::read_sub_struct(0x28C7B799);
    case kCombo:
        return script::read_sub_struct(0xFECBAB7C);
    case 0x0FC6F698:
        return script::read_sub_struct(0x6626B0D8);
    case kCone:
        return script::read_sub_struct(0xFD6CAF2A);
    case kSkate:
        return script::read_sub_struct(0xE41E0E04);
    case 0x61C5D092:
        return script::read_sub_struct(0x9005703A);
    case 0xEA7BA666:
        return script::read_sub_struct(0xAD585D92);
    case 0x7FE2238A:
        return script::read_sub_struct(0x38C1D87E);
    case 0xD0DD047B: {
        script::Struct* defaults = script::read_sub_struct(0x97FEFF8F);
        script::run_script_once(0xFA54B475, nullptr, nullptr, defaults);
        return defaults;
    }
    case 0xEC414B76:
        return script::read_sub_struct(0xAB62B082);
    default:
        return nullptr;
    }
}

// 0x004E69C0
int __cdecl goal_editor_constant82() { return 0x82; }

// 0x004E69D0
void __fastcall init_edited_goal(uint8_t* goal)
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
void __stdcall run_marker_object_script(int index)
{
    script::Struct params;
    params.prepare_all();
    char* name = g_goal_name_buffer;
    get_sprintf(name, "GoalEditorMarkerObject%d", index);
    params.put_name_key(0xA1DC81F9, checksums::hash_from_text(name));
    script::launch_script(0xDCF3A25E, &params, nullptr, false, nullptr);
    params.get_clear();
}

} // namespace park_editing
