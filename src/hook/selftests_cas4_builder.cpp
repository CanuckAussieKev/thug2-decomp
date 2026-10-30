// Self-tests for the model builder (src/game/gfx/model_builder.cpp) and Mth::Matrix::TranslateLocal: original and ours
// on a builder in an arena, with the model, skeleton, appearance copy, texture dictionaries, structures, scripts and
// CObject's code stood in by recording code (a builder whose vtable's steps record themselves, so CallMemberFunction
// is seen choosing them); random sequences of every member, the calls, results and memory compared.

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/gfx/engine_texture.h"
#include "game/gfx/model.h"
#include "game/gfx/model_appearance.h"
#include "game/gfx/model_builder.h"
#include "game/gfx/skeleton.h"
#include "game/gfx/texture_dict.h"
#include "game/mth/matrix.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/cas4_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace Cas4Tests;
using Gfx::CModelAppearance;
using Gfx::CModelBuilder;

uint32_t Bits(float f) { return std::bit_cast<uint32_t>(f); }

Lcg* g_rand; // the stand-ins' answers
std::map<uint32_t, uint32_t> g_parts, g_options; // the appearance copy's slots / their CAS options, by part
void* g_builderSlots[26];
void* g_textureSlots[42];
void* g_appearanceSlots[8];
uint32_t g_textures[4]; // fake textures handed out (each 0xC bytes, the fake vtable)
int g_textureIndex;
int g_updates; // the script runs this many updates

uint32_t Answer() { return g_rand->Below(4) ? 1u : 0u; }
void RecordVector(uint32_t at)
{
    const auto* v = reinterpret_cast<const uint32_t*>(at);
    Record({v[0], v[1], v[2], v[3]});
}

// The names the steps read (all values random: checksums, numbers, pointers alike).
constexpr uint32_t kNames[] = {
    0xB6F08F39, 0x83418A6A, 0x318F2BDB, 0xB990D003, 0xE6530EF0, 0x564E3BC0, 0x9FBBDB72, 0xA01371B1, 0x92B43E79,
    0xC9AED80F, 0x1E90C5A9, 0xFECA8BB3, 0x67C3DA09, 0x10C4EA9F, 0x8EA07F3C, 0xC6CC7F7F, 0x4CA9B693, 0x3CC3421C,
    0x6E94F918, 0xE4F130F4, 0x949BC47B, 0x6AA8D676, 0xCCDFDDC2, 0x676F1DF1, 0xC553C612, 0xC876ADF3, 0x25A0E7E9,
    0x499F4BE9, 0x49B318FD, 0x7323E97C, 0x0424D9EA, 0x9D2D8850, 0xCF6AA087, 0x5663F13D, 0x266932C8, 0x1256B6C6,
    0x5A96985D, 0x8602F6EE, 0x97DBDDE6, 0x812684EF, 0x1ADE0DD8, 0x3F5F5BC2, 0x00000000, 0xF362DBBA, 0x7A993873,
    0x9B03ADAD, 0xED189051, 0xE39069C9, 0x020AFC17, 0x7411C1EB, 0x9497595F, 0x750DCC81, 0x0316F17D, 0x0AF3CCFC,
    0xEB695922, 0x9D7264DE, 0x7DF4FC6A, 0x9C6E69B4, 0xEA755448, 0xE4FDADD0, 0x0567380E, 0x737C05F2, 0x13B9DA7B,
};
constexpr uint32_t kParts[] = {0x650FAB6D, 0x7E54E2A2, 0x0571DA24, 0x0FC85BAE, 0x7C53B658, 0x74E37E2A,
                               0xA921D236, 0xEB307E68, 0x2457F44D, 0x8B314358, 0x00C0FFEE};
constexpr uint32_t kDescIds[] = {0x64E9DAEF, 0x92986E0D, 0x79DAA2F9, 0x806FFF30, 0x99D1CC07, 0x12345678};
constexpr uint32_t kFlags[] = {0xB25E39FD, 0xF11C6E74, 0x3C4D849D};
constexpr uint32_t kMembers[] = {0x18924DA1, 0x0ECF0248, 0x21AEC583, 0x30E2951D, 0x3BF41299, 0x50F1285B,
                                 0x53DA94F1, 0x6516B484, 0x681A03AF, 0x705117E1, 0x73AB02BC, 0x9041E901,
                                 0xAA42A104, 0xAEE6E915, 0xB1C39A54, 0xBFF957A9, 0xC0BC8271, 0xCDEEA636,
                                 0xCEFE8478, 0xDD9C297E, 0x6C63C7C5};
constexpr uint32_t kBones[] = {0xB0E1, 0xB0E2, 0xB0E3, 0xB0E4};

// A stand-in structure with random values.
uint32_t MakeStruct(Lcg& random, int density)
{
    uint32_t s = Bump(8);
    for (uint32_t name : kNames) {
        if (random.Below(density) == 0)
            continue;
        uint32_t value;
        switch (random.Below(6)) {
        case 0: value = random.Below(2); break;
        case 1: value = random.Pick(kDescIds); break;
        case 2: value = Bits(static_cast<float>(random.Below(4000)) * 0.125f - 250.0f); break;
        case 3: value = random.Below(300); break;
        case 4: value = random.Pick(kParts); break;
        default: value = random.Next(); break;
        }
        SetValue(reinterpret_cast<void*>(s), name, value);
    }
    if (random.Below(5)) // (a missing desc_id keeps the previous one: PropagateHeadColor's hands)
        SetValue(reinterpret_cast<void*>(s), 0x4BB2084E, random.Pick(kDescIds));
    for (uint32_t flag : kFlags) {
        if (random.Below(2))
            g_flags.insert({s, flag});
    }
    return s;
}
// The values the original would read as stack garbage when missing, always given.
void FillRequired(uint32_t s, Lcg& random)
{
    for (uint32_t name : {0x6E94F918u, 0xE4F130F4u, 0x949BC47Bu, 0xC6CC7F7Fu, 0x4CA9B693u, 0x3CC3421Cu, 0xCF6AA087u,
                          0x5663F13Du, 0x266932C8u, 0x1256B6C6u, 0xA01371B1u, 0x6AA8D676u, 0xCCDFDDC2u, 0x676F1DF1u,
                          0xC553C612u, 0x49B318FDu, 0x5A96985Du, 0x8602F6EEu}) {
        if (!g_values.count({s, name}))
            SetValue(reinterpret_cast<void*>(s), name,
                     name == 0x5A96985Du || name == 0x8602F6EEu ? random.Below(3) == 0
                                                               : random.Below(200));
    }
    if (random.Below(3) == 0) { // (a head without its own colour: its part's defaults)
        g_values.erase({s, 0x6E94F918u});
    }
}

void StandInBuilderCalls(StandIns& s)
{
    StandInMemory(s);
    StandInStructs(s);
    StandInStructPool(s);
    using Calloc3 = void* (*)(unsigned int, Mem::Heap*, int);
    s.At({0x00401D40, AddressOf(static_cast<Calloc3>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0x1D40, r.args[0], r.args[1], r.args[2]});
        uint32_t at = Bump(r.args[0]);
        if (at)
            std::memset(reinterpret_cast<void*>(at), 0, r.args[0]);
        return at;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Record({0xB0E0, Relocate(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs& r) {
        Record({0xB110, Relocate(r.ecx)});
        return 0u;
    });
    using CallMember = bool (Obj::CObject::*)(uint32_t, Script::CStruct*, Script::CScript*);
    s.At({0x00460B80, AddressOf(&Obj::CObject::Construct)}, 0, [](Regs& r) {
        Record({0x0B80, Relocate(r.ecx)});
        *reinterpret_cast<uint32_t*>(r.ecx) = 0x006477E8;
        return r.ecx;
    });
    s.At({0x004615D0, AddressOf(&Obj::CObject::DestroyBody)}, 0, [](Regs& r) {
        Record({0x15D0, Relocate(r.ecx), Relocate(*reinterpret_cast<uint32_t*>(r.ecx))});
        return 0u;
    });
    s.At({0x004608F0, AddressOf(static_cast<CallMember>(&Obj::CObject::CallMemberFunction))}, 12, [](Regs& r) {
        Record({0x08F0, Relocate(r.ecx), r.args[0], Relocate(r.args[1]), Relocate(r.args[2])});
        return Answer();
    });
    using Run = void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*);
    s.At({0x00475790, AddressOf(static_cast<Run>(&Script::RunScript))}, 0, [](Regs& r) {
        Record({0x5790, r.args[0], Relocate(r.args[1]), Relocate(r.args[2]), r.args[3] & 0xFF, r.args[4]});
        return 0u;
    });
    s.At({0x00476900, AddressOf(&Script::CStruct::GetScript)}, 12, [](Regs& r) { // (CStruct's GetScript)
        Record({0x6900, Relocate(r.ecx), r.args[0], Relocate(r.args[1]), r.args[2]});
        auto it = g_values.find({r.ecx, r.args[0]});
        if (it == g_values.end())
            return 0u;
        if (r.args[1]) {
            reinterpret_cast<uint32_t*>(r.args[1])[0] = r.args[0];
            reinterpret_cast<uint32_t*>(r.args[1])[1] = it->second;
        }
        return 1u;
    });
    // the appearance copy
    s.At({0x00489050, AddressOf(&CModelAppearance::GetPartOption)}, 4, [](Regs& r) {
        Record({0x9050, Relocate(r.ecx), r.args[0]});
        auto it = g_options.find(r.args[0]);
        return it == g_options.end() ? 0u : it->second;
    });
    s.At({0x004890B0, AddressOf(&CModelAppearance::GetPart)}, 4, [](Regs& r) {
        Record({0x90B0, Relocate(r.ecx), r.args[0]});
        auto it = g_parts.find(r.args[0]);
        return it == g_parts.end() ? 0u : it->second;
    });
    s.At({0x00488EA0, AddressOf(&CModelAppearance::Construct)}, 0, [](Regs& r) {
        Record({0x8EA0, Relocate(r.ecx)});
        *reinterpret_cast<void***>(r.ecx) = g_appearanceSlots;
        return r.ecx;
    });
    s.At({0x00488F00, AddressOf(&CModelAppearance::Reset)}, 0, [](Regs& r) {
        Record({0x8F00, Relocate(r.ecx)});
        return 1u;
    });
    s.At({0x004893A0, AddressOf(&CModelAppearance::CopyFrom)}, 4, [](Regs& r) {
        Record({0x93A0, Relocate(r.ecx), Relocate(r.args[0])});
        return r.ecx;
    });
    s.At({0x00487510}, 0, [](Regs& r) {
        Record({0x7510, Relocate(r.ecx)});
        return static_cast<uint32_t>(*reinterpret_cast<uint8_t*>(r.ecx));
    });
    s.At({0x00487B00}, 0, [](Regs& r) { // (the scale a structure gives)
        Record({0x7B00, Relocate(r.args[1])});
        RecordVector(r.args[0]);
        if (!Answer())
            return 0u;
        auto* v = reinterpret_cast<float*>(r.args[0]);
        for (int i = 0; i < 4; ++i)
            v[i] = static_cast<float>(g_rand->Below(300)) * 0.01f + 0.3f;
        return 1u;
    });
    // the model
    auto model = [&s](uint32_t game, uint32_t ours, int args, auto record) {
        s.At({game, ours}, static_cast<uint16_t>(4 * args), [game, record](Regs& r) {
            Record({game & 0xFFFF, Relocate(r.ecx)});
            record(r);
            return Answer();
        });
    };
    using namespace Nx;
    model(0x00492EE0, AddressOf(&CModel::SetScale), 1, [](Regs& r) { RecordVector(r.args[0]); });
    model(0x004935E0, AddressOf(&CModel::AddFileGeom), 8, [](Regs& r) {
        Record({r.args[0], r.args[1], r.args[2] & 0xFF, r.args[3], r.args[4] & 0xFF, r.args[5] & 0xFF, r.args[6] & 0xFF,
                r.args[7] & 0xFF});
    });
    model(0x00493D10, AddressOf(&CModel::ReplaceTexture), 3, [](Regs& r) { Record({r.args[0], r.args[1], r.args[2]}); });
    model(0x00493E80, AddressOf(&CModel::ReplaceTextureWith), 3,
          [](Regs& r) { Record({r.args[0], r.args[1], r.args[2]}); });
    model(0x00494160, AddressOf(&CModel::SetUVMatrix), 3, [](Regs& r) {
        Record({r.args[0], r.args[1]});
        for (int i = 0; i < 4; ++i)
            RecordVector(r.args[2] + 16 * i);
    });
    model(0x004941B0, AddressOf(&CModel::AllocateUVMatrixParams), 2, [](Regs& r) { Record({r.args[0], r.args[1]}); });
    model(0x00494200, AddressOf(&CModel::SetColorFromScript), 4,
          [](Regs& r) { Record({Relocate(r.args[0]), r.args[1], r.args[2], r.args[3]}); });
    model(0x00494450, AddressOf(&CModel::ClearColorFromScript), 1, [](Regs& r) { Record({Relocate(r.args[0])}); });
    model(0x00494700, AddressOf(&CModel::CleanInstanceStrips), 0, [](Regs&) {});
    model(0x00494800, AddressOf(&CModel::HideGeom), 2, [](Regs& r) { Record({r.args[0], r.args[1] & 0xFF}); });
    model(0x00494A00, AddressOf(&CModel::ReplaceFaceTexture), 3,
          [](Regs& r) { Record({Relocate(r.args[0]), r.args[1], r.args[2]}); });
    model(0x00494B30, AddressOf(&CModel::BreakOwnStrips), 0, [](Regs&) {});
    model(0x00494BA0, AddressOf(&CModel::ClearGeoms), 0, [](Regs&) {});
    // the skeleton
    model(0x004A2CE0, AddressOf(&Gfx::CSkeleton::ResetBoneScales), 0, [](Regs&) {});
    model(0x004A2D20, AddressOf(&Gfx::CSkeleton::GetBoneScale), 2, [](Regs& r) {
        Record({r.args[0], reinterpret_cast<uint32_t*>(r.args[1])[3]}); // (x, y, z come uninitialised: only w)
        auto* v = reinterpret_cast<float*>(r.args[1]);
        for (int i = 0; i < 4; ++i)
            v[i] = static_cast<float>(g_rand->Below(500)) * 0.01f;
    });
    model(0x004A2D80, AddressOf(&Gfx::CSkeleton::SetBoneScale), 3, [](Regs& r) {
        Record({r.args[0], r.args[2] & 0xFF});
        RecordVector(r.args[1]);
    });
    model(0x004A3570, AddressOf(&Gfx::CSkeleton::ApplyBoneScales), 1, [](Regs& r) { Record({Relocate(r.args[0])}); });
    // the texture dictionaries and textures
    s.At({0x0049F9B0, AddressOf(&CTexDict::LoadTexture)}, 16, [](Regs& r) {
        Record({0xF9B0, Relocate(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3] & 0xFF});
        return g_textures[g_textureIndex++ & 3];
    });
    s.At({0x0049F540, AddressOf(&CTexDict::GetTexture)}, 4, [](Regs& r) {
        Record({0xF540, Relocate(r.ecx), r.args[0]});
        return g_textures[g_textureIndex++ & 3];
    });
    s.At({0x0049F500, AddressOf(&CTexDict::UnloadTexture)}, 4, [](Regs& r) {
        Record({0xF500, Relocate(r.ecx), Relocate(r.args[0])});
        return 1u;
    });
    const int textureArgs[42] = {0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 0, 0, 0, 0, 0, 2, 2, 3, 0, 1, 0,
                                 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
    for (int slot = 0; slot < 42; ++slot) {
        g_textureSlots[slot] = s.Thunk(static_cast<uint16_t>(4 * textureArgs[slot]), [slot, textureArgs](Regs& r) {
            Record({0x7E0000u + static_cast<uint32_t>(slot), Relocate(r.ecx)});
            for (int i = 0; i < textureArgs[slot]; ++i)
                Record({Relocate(r.args[i])});
            return Answer();
        });
    }
    // the builder's steps (its vtable, for CallMemberFunction) and the copy's destructor
    const int stepArgs[26] = {1, 1, 3, 3, 0, 1, 0, 0, 4, 1, 1, 2, 1, 3, 2, 1, 1, 0, 0, 0, 0, 2, 0, 0, 0, 2};
    for (int slot = 0; slot < 26; ++slot) {
        g_builderSlots[slot] = s.Thunk(static_cast<uint16_t>(4 * stepArgs[slot]), [slot, stepArgs](Regs& r) {
            Record({0xB10000u + static_cast<uint32_t>(slot), Relocate(r.ecx)});
            for (int i = 0; i < stepArgs[slot]; ++i)
                Record({slot == 11 && i == 1 ? r.args[i] & 0xFF : Relocate(r.args[i])}); // (a bool)
            return Answer();
        });
    }
    g_appearanceSlots[0] = s.Thunk(4, [](Regs& r) {
        Record({0xA90000, Relocate(r.ecx), r.args[0]});
        return r.ecx;
    });
    // the script RunScript makes
    s.At({0x004738E0, AddressOf(&Script::CScript::Construct)}, 0, [](Regs& r) {
        Record({0x38E0, Relocate(r.ecx)});
        return r.ecx;
    });
    s.At({0x00474720, AddressOf(&Script::CScript::SetScriptCopy)}, 12, [](Regs& r) {
        Record({0x4720, Relocate(r.ecx), Relocate(r.args[0]), r.args[1], Relocate(r.args[2])});
        if (r.args[0])
            RecordVector(r.args[0]); // (the held script's two words and two beyond)
        return 0u;
    });
    s.At({0x004750B0, AddressOf(&Script::CScript::Update)}, 0, [](Regs& r) {
        Record({0x50B0, Relocate(r.ecx)});
        return g_updates-- > 0 ? 1u : 0u;
    });
    s.At({0x00474420, AddressOf(&Script::CScript::Destroy)}, 0, [](Regs& r) {
        Record({0x4420, Relocate(r.ecx)});
        return 0u;
    });
    s.At({0x004499B0, AddressOf(static_cast<void (*)(void*)>(&Script::CScript::operator delete))}, 0, [](Regs& r) {
        Record({0x99B0, Relocate(r.args[0])});
        return 0u;
    });
}

// Random builders driven through every member (both runs alike); each result recorded.
void TestModelBuilder()
{
    GameMemory& memory = *new GameMemory({{0x007CC979, 1}, {0x007D1800, 4}, {0x006F3614, 16}, {0x006F3548, 16},
                                          {0x006FA2F8, 12}, {0x00700F1C, 8}});
    StandIns& s = *new StandIns();
    StandInBuilderCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 80; ++round) {
        ok &= CompareRuns("model builder", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            ClearStructs();
            g_parts.clear();
            g_options.clear();
            memory.Zero();
            Lcg random(0x5B00 + round);
            Lcg answers(0x6C00 + round);
            g_rand = &answers;
            g_textureIndex = 0;
            GameVariable<uint8_t>(0x007CC979) = 1;
            uint32_t manager = Bump(0x600);
            *reinterpret_cast<uint32_t*>(manager + 0x110) = 0x7D0110;
            GameVariable<uint32_t>(0x007D1800) = manager;
            UseStructPool(round & 1);
            reinterpret_cast<uint32_t*>(0x006F3548)[0] = 1;
            reinterpret_cast<uint32_t*>(0x006F3548)[3] = round & 2 ? 0x5B002000u : 0;
            for (uint32_t& texture : g_textures) {
                texture = Bump(0xC);
                *reinterpret_cast<void***>(texture) = g_textureSlots;
            }
            GameVariable<uint32_t>(0x00700F1C) = Bump(0x10);
            GameVariable<uint32_t>(0x00700F20) = Bump(0x10);
            uint32_t model = Bump(0x50);
            for (int i = 0; i < 4; ++i)
                reinterpret_cast<float*>(model + 0x1C)[i] = static_cast<float>(random.Below(400)) * 0.01f;
            uint32_t skeleton = round % 7 == 3 ? 0 : Bump(0x40);
            auto* copy = reinterpret_cast<CModelAppearance*>(Bump(sizeof(CModelAppearance)));
            *reinterpret_cast<void***>(copy) = g_appearanceSlots;
            uint32_t face = Bump(0x10);
            *reinterpret_cast<uint8_t*>(face) = static_cast<uint8_t>(random.Below(2));
            copy->mp_faceTexture = random.Below(3) ? reinterpret_cast<Gfx::CFaceTexture*>(face) : nullptr;
            copy->m_hasFaceTexture = random.Below(2) != 0;
            auto setGlobals = [&]() {
                GameVariable<uint32_t>(0x006FA2F8) = model;
                GameVariable<uint32_t>(0x006FA2FC) = skeleton;
                GameVariable<CModelAppearance*>(0x006FA300) = copy;
            };
            setGlobals();
            // the copy's slots and their options
            for (uint32_t part : kParts) {
                if (part == 0x650FAB6D || part == 0x0FC85BAE ? random.Below(2) != 0 : random.Below(4) != 0) {
                    uint32_t slot = MakeStruct(random, 3);
                    FillRequired(slot, random);
                    g_parts[part] = slot;
                }
                if (random.Below(4) || part == 0x650FAB6D || part == 0x0FC85BAE) {
                    uint32_t option = MakeStruct(random, 3);
                    FillRequired(option, random);
                    g_options[part] = option;
                }
            }
            SetValue(&copy->m_structure, 0x812684EF, random.Below(2) ? MakeStruct(random, 3) : 0);
            if (!g_values[{Ptr(&copy->m_structure), 0x812684EF}])
                g_values.erase({Ptr(&copy->m_structure), 0x812684EF});
            // the bone lists (global arrays named after the parts; the non-local bones)
            for (uint32_t part : kParts) {
                if (random.Below(3) == 0)
                    continue;
                auto* array = reinterpret_cast<Script::CArray*>(Bump(sizeof(Script::CArray)));
                uint32_t count = random.Below(4);
                auto* elements = reinterpret_cast<uint32_t*>(Bump(16));
                for (uint32_t i = 0; i < 4; ++i)
                    elements[i] = random.Pick(kBones);
                array->m_unknown0 = 0;
                array->m_type = Script::ESYMBOLTYPE_NAME;
                array->m_unknown2 = 0;
                array->m_size = count;
                if (count == 1)
                    array->m_single = elements[0];
                else
                    array->m_elements = elements;
                g_globalArrays[part] = Ptr(array);
            }
            if (random.Below(3)) {
                auto* array = reinterpret_cast<Script::CArray*>(Bump(sizeof(Script::CArray)));
                auto* elements = reinterpret_cast<uint32_t*>(Bump(16));
                elements[0] = kBones[1];
                elements[1] = kBones[3];
                array->m_unknown0 = 0;
                array->m_type = Script::ESYMBOLTYPE_NAME;
                array->m_unknown2 = 0;
                array->m_size = 2;
                array->m_elements = elements;
                g_globalArrays[0x20D9AC2F] = Ptr(array);
            }
            auto* builder = reinterpret_cast<CModelBuilder*>(Bump(sizeof(CModelBuilder)));
            std::vector<uint32_t> out;
            out.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, bool, int)>(original, 0x004897D0)(
                builder, random.Below(2) != 0, static_cast<int>(random.Below(3)))));
            auto* script = reinterpret_cast<uint8_t*>(Bump(0x100));
            for (int step = 0; step < 30; ++step) {
                uint32_t op = random.Below(30);
                op = op >= 23 ? 0 : op; // (member functions more often)
                out.push_back(0xC0DE0000 + op);
                uint32_t part = random.Pick(kParts);
                auto call0 = [&](uint32_t address) {
                    out.push_back(Fn<uint8_t(__thiscall*)(void*)>(original, address)(builder));
                };
                auto call1 = [&](uint32_t address, uint32_t a) {
                    out.push_back(Fn<uint8_t(__thiscall*)(void*, uint32_t)>(original, address)(builder, a));
                };
                auto call2 = [&](uint32_t address, uint32_t a, uint32_t b) {
                    out.push_back(Fn<uint8_t(__thiscall*)(void*, uint32_t, uint32_t)>(original, address)(builder, a, b));
                };
                switch (op) {
                case 0: { // a member function, with the step slots recording themselves
                    void** saved = builder->m_vtable;
                    builder->m_vtable = g_builderSlots;
                    uint32_t params = MakeStruct(random, 2);
                    uint32_t member = random.Pick(kMembers);
                    if (member == 0x73AB02BC && random.Below(3) == 0) // (ModelApplyFaceTexture with no model)
                        GameVariable<uint32_t>(0x006FA2F8) = 0;
                    out.push_back(Fn<uint8_t(__thiscall*)(void*, uint32_t, uint32_t, void*)>(original, 0x0048A3B0)(
                        builder, member, params, script));
                    GameVariable<uint32_t>(0x006FA2F8) = model;
                    builder->m_vtable = saved;
                    break;
                }
                case 1: call1(0x00489820, part); break;
                case 2: call1(0x0048A9C0, part); break;
                case 3: call2(0x00489800, random.Next(), random.Below(2)); break;
                case 4: {
                    uint32_t params = MakeStruct(random, 3);
                    SetValue(reinterpret_cast<void*>(params), 0xB6F08F39, part);
                    call1(0x00489A50, params);
                    break;
                }
                case 5:
                    out.push_back(Fn<uint8_t(__thiscall*)(void*, uint32_t, uint32_t, int)>(original, 0x0048B030)(
                        builder, part, random.Next(), static_cast<int>(random.Below(3))));
                    break;
                case 6: call2(0x00489C30, random.Next(), random.Below(3)); break;
                case 7: call1(0x00489C50, part); break;
                case 8: call1(0x0048ADD0, part); break;
                case 9: call0(0x00489A30); break;
                case 10: call0(0x00489A40); break;
                case 11: call0(0x0048A9E0); break;
                case 12: call0(0x0048AA30); break;
                case 13: call2(0x0048AA70, part, random.Below(3) ? random.Pick(kParts) : 0); break;
                case 14: call0(0x0048AC40); break;
                case 15: call0(0x0048A220); break;
                case 16: call0(0x00489F10); break;
                case 17:
                    g_updates = static_cast<int>(random.Below(4));
                    if (round % 9 == 4 && random.Below(2))
                        g_failAllocations.push_back(true); // (no memory for the held script)
                    call2(0x0048B200, part, random.Pick(kNames));
                    g_failAllocations.clear();
                    break;
                case 18: { // built
                    uint32_t from = Bump(sizeof(CModelAppearance));
                    Fn<void(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t)>(original, 0x0048A2F0)(
                        builder, from, model, skeleton, random.Next());
                    for (int i = 0; i < 3; ++i)
                        out.push_back(ArenaRelocate(reinterpret_cast<uint32_t*>(0x006FA2F8)[i]));
                    setGlobals();
                    break;
                }
                case 19: // destroyed, and made again
                    out.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x0048A2C0)(
                        builder, random.Below(2) | 2u)));
                    out.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, bool, int)>(original, 0x004897D0)(
                        builder, random.Below(2) != 0, 7)));
                    break;
                case 20: // the copy's face texture changed
                    *reinterpret_cast<uint8_t*>(face) = static_cast<uint8_t>(random.Below(2));
                    copy->m_hasFaceTexture = random.Below(2) != 0;
                    break;
                case 21: { // the skeleton gone or back
                    skeleton = skeleton ? 0 : Bump(0x40);
                    GameVariable<uint32_t>(0x006FA2FC) = skeleton;
                    break;
                }
                default: { // TranslateLocal
                    auto* m = reinterpret_cast<Mth::Matrix*>(Bump(sizeof(Mth::Matrix)));
                    for (int i = 0; i < 16; ++i)
                        (&m->row[0].x)[i] = static_cast<float>(random.Below(2000)) * 0.37f - 300.0f;
                    Mth::Vector v = {static_cast<float>(random.Below(100)) * 0.7f - 20.0f,
                                     static_cast<float>(random.Below(100)) * 0.3f, -1.75f, 9.0f};
                    if (random.Below(2)) { // (terms that cancel: the order of the sums shows)
                        for (int c = 0; c < 3; ++c) {
                            (&m->row[0].x)[c] = 1.0f;
                            (&m->row[1].x)[c] = 1e8f;
                            (&m->row[2].x)[c] = random.Below(2) ? -1e8f : 1e8f;
                        }
                        v = {0.001f, 1e8f, random.Below(2) ? 1e8f : -1e8f, 0.0f};
                    }
                    out.push_back(ArenaRelocate(
                        Fn<uint32_t(__thiscall*)(void*, const Mth::Vector*)>(original, 0x0048A260)(m, &v)));
                    break;
                }
                }
                out.insert(out.end(), g_trace.begin(), g_trace.end());
                g_trace.clear();
            }
            DumpArena(out);
            memory.Dump(out);
            return out;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "model builder: made, built, member functions choosing their steps, every step (geoms, colours, UVs, "
              "textures, created graphics, scales, bone scales, head colour, scripts), destroyed; TranslateLocal");
}

} // namespace

SELFTEST(Early, TestModelBuilder, RemoveStandIns);
