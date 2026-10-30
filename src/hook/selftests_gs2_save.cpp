// Self-tests for the save types and the memory card commands' helpers (save_types.cpp) and the save state commands
// (memcard_cfuncs.cpp): each original against ours on the same inputs, the game functions they call stood in for by
// recording code. No file is touched.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/skate/skate_module.h"
#include "game/core/alloc.h"
#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/pool.h"
#include "game/ed/park_editor.h"
#include "game/mc/memcard.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/object.h"
#include "game/obj/tracker.h"
#include "game/pcm/music.h"
#include "game/pcm/stream_commands.h"
#include "game/mc/memcard_cfuncs.h"
#include "game/mc/save_types.h"
#include "game/script/struct.h"
#include "game/script/struct_buffer.h"
#include "game/skate/profile_manager.h"
#include "game/skater/skater.h"
#include "game/goals/goal_manager.h"
#include "hook/cfunc_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using namespace CFuncTests;
using Script::CStruct;

const uint32_t kTypes[] = {Mc::kSaveOptionsAndPros, Mc::kSaveCas,         Mc::kSavePark,
                           Mc::kSaveNetworkSettings, Mc::kSaveCat,        Mc::kSaveCreatedGoals,
                           Mc::kSaveCreatedGraphic,  Mc::kSaveReplay,     0x12345678u, 0u};

alignas(16) uint8_t g_skaters[2][0x200];
alignas(16) uint8_t g_profiles[0x40];
alignas(16) uint8_t g_goalManager[0x40];
alignas(16) uint8_t g_ownParams[0x10];
alignas(16) uint8_t g_newStruct[0x10];
uint32_t g_profileIndex;
uint32_t g_readCustomResult;

uint32_t Mark(uint32_t value)
{
    if (value == reinterpret_cast<uint32_t>(g_skaters[0]))
        return 0x5C0;
    if (value == reinterpret_cast<uint32_t>(g_skaters[1]))
        return 0x5C1;
    if (value == reinterpret_cast<uint32_t>(g_profiles))
        return 0x9F0;
    if (value == reinterpret_cast<uint32_t>(g_goalManager))
        return 0x6A0;
    if (value == reinterpret_cast<uint32_t>(g_ownParams))
        return 0x0A0;
    if (value == reinterpret_cast<uint32_t>(g_newStruct))
        return 0x057;
    return value;
}

void StandInCalls(StandIns& s)
{
    using GetIntegerFn = int (*)(uint32_t);
    s.At({0x00478F60, AddressOf(static_cast<GetIntegerFn>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x8F60, r.args[0]});
        return (r.args[0] >> 20) * 3 + 0x37;
    });
    using GetIntegerNamedFn = int (*)(const char*);
    s.At({0x004790C0, AddressOf(static_cast<GetIntegerNamedFn>(&Script::GetInteger))}, 0, [](Regs& r) {
        uint32_t hash = Hash(reinterpret_cast<const char*>(r.args[0]));
        Record({0x90C0, hash});
        return hash & 0xFFF;
    });
    s.At({0x006234FE}, 0, StandInSprintf);
    s.At({0x005B5F10}, 0, [](Regs&) {
        Record({0x5F10});
        return reinterpret_cast<uint32_t>("BASLUS-21012");
    });
    // ApplyGoalManagerParams
    s.At({0x005591B0, AddressOf(Game::GetGoalManager)}, 0, [](Regs&) { Record({0x91B0}); return reinterpret_cast<uint32_t>(g_goalManager); });
    s.At({0x00557E70}, 4, [](Regs& r) { Record({0x7E70, Mark(r.ecx), r.args[0]}); return 0u; });
    s.At({0x004A2BA0}, 0, [](Regs& r) { Record({0x2BA0, Mark(r.ecx)}); return reinterpret_cast<uint32_t>(g_ownParams); });
    s.At({0x00477090, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) { Record({0x7090, Mark(r.ecx)}); return 0u; });
    s.At({0x00477130}, 0, [](Regs& r) { Record({0x7090, Mark(r.ecx)}); return 0u; });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Record({0x8540, Mark(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00558A00}, 0, [](Regs& r) { Record({0x8A00, Mark(r.ecx)}); return 0x7701u; });
    // ApplyCatAndCustomSkater
    s.At({0x00553E90}, 0, [](Regs& r) { Record({0x3E90, Mark(r.ecx)}); return g_profileIndex; });
    s.At({0x005665C0, AddressOf(&Mdl::Skate::GetSkater)}, 4, [](Regs& r) {
        Record({0x65C0, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_skaters[r.args[0] & 1]);
    });
    s.At({0x00580060, AddressOf(&Obj::CSkater::LoadCATInfo)}, 4, [](Regs& r) {
        Record({0x0060, Mark(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00580150, AddressOf(&Obj::CSkater::LoadList44B66A9F)}, 4, [](Regs& r) {
        Record({0x0150, Mark(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00580210, AddressOf(&Obj::CSkater::ReadChapterInfo)}, 4, [](Regs& r) {
        Record({0x0210, Mark(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x0057BD70, AddressOf(&Obj::CPlayerProfileManager::ReadCustomSkater)}, 8, [](Regs& r) {
        Record({0xBD70, Mark(r.ecx), r.args[0], r.args[1] & 0xFF});
        return g_readCustomResult;
    });
    // KeepLoadedContents
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) { Record({0x9070, Mark(r.args[0])}); return 0u; });
    s.At({0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, [](Regs& r) {
        Record({0xA3D0, r.ecx});
        return reinterpret_cast<uint32_t>(g_newStruct);
    });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xAFC0, r.args[0], r.args[1], r.args[2]});
        return reinterpret_cast<uint32_t>(g_newStruct);
    });
    s.At({0x0047A0C0, AddressOf(&Script::ReadFromBuffer)}, 0, [](Regs& r) {
        Record({0xA0C0, Mark(r.args[0]), r.args[1]});
        return r.args[1] + 0x33;
    });
    // the destructors
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) { Record({0xABA0, r.args[0]}); return 0u; });
    // SetSectionsToApplyWhenLoading's message
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0x1C30, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
}

// The register entry points (the original's address or our stub).
uint32_t CallMakeFileName(void* code, const char* name, char* buffer, uint32_t typeValue)
{
    uint32_t result;
    __asm {
        push esi
        push edi
        mov eax, name
        mov esi, buffer
        mov edi, typeValue
        call code
        mov result, eax
        pop edi
        pop esi
    }
    return result;
}
uint32_t CallEax(void* code, uint32_t value)
{
    uint32_t result;
    __asm {
        mov eax, value
        call code
        mov result, eax
    }
    return result;
}
// TypeFromLetter: the result, with whether ECX and EDX came back unchanged (DeleteMemCardFile relies on EDX).
uint32_t CallLetter(void* code, uint32_t value)
{
    uint32_t result;
    uint32_t ecxAfter;
    uint32_t edxAfter;
    __asm {
        mov eax, value
        mov ecx, 0x0EC0EC0E
        mov edx, 0x0ED0ED0E
        call code
        mov result, eax
        mov ecxAfter, ecx
        mov edxAfter, edx
    }
    return result ^ (ecxAfter == 0x0EC0EC0E ? 0 : 1) ^ (edxAfter == 0x0ED0ED0E ? 0 : 2);
}
uint32_t CallEaxEcx(void* code, uint32_t eaxValue, uint32_t ecxValue)
{
    uint32_t result;
    __asm {
        mov eax, eaxValue
        mov ecx, ecxValue
        call code
        mov result, eax
    }
    return result;
}
uint32_t CallEcx(void* code, uint32_t value)
{
    uint32_t result;
    __asm {
        mov ecx, value
        call code
        mov result, eax
    }
    return result;
}
uint32_t CallEsi(void* code, uint32_t value)
{
    uint32_t result;
    __asm {
        push esi
        mov esi, value
        call code
        mov result, eax
        pop esi
    }
    return result;
}
uint32_t CallEdiEsi(void* code, uint32_t ediValue, uint32_t esiValue)
{
    uint32_t result;
    __asm {
        push esi
        push edi
        mov edi, ediValue
        mov esi, esiValue
        call code
        mov result, eax
        pop edi
        pop esi
    }
    return result;
}
uint32_t CallEdiStack2(void* code, uint32_t ediValue, uint32_t first, uint32_t second)
{
    uint32_t result;
    __asm {
        push edi
        mov edi, ediValue
        push second
        push first
        call code
        add esp, 8
        mov result, eax
        pop edi
    }
    return result;
}
uint32_t CallEdiStack1(void* code, uint32_t ediValue, uint32_t first)
{
    uint32_t result;
    __asm {
        push edi
        mov edi, ediValue
        push first
        call code
        add esp, 4
        mov result, eax
        pop edi
    }
    return result;
}

void* Code(bool original, uint32_t address, void (*stub)())
{
    return original ? OriginalFunction(address) : reinterpret_cast<void*>(stub);
}

void TestSaveTypes()
{
    std::vector<Saved> saved = {{0x007CEFA8, 4, {}}, {0x007CECE8, 0x100, {}}, {0x007CECD0, 0x14, {}},
                                {0x007CECE0, 4, {}}, {0x007CE478, 4, {}},    {0x007CE4B4, 4, {}},
                                {0x006F3614, 16, {}}, {0x007CC979, 1, {}},   {0x007D1800, 4, {}},
                                {0x006F3624, 4, {}}};
    SaveAll(saved);
    UseEmptySymbolTable();
    auto* standIns = new StandIns;
    StandInCalls(*standIns);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        // MakeFileName: names and types
        for (const char* name : {"", "a", "Tony Hawk", "my park 01", "zzzzzzzzzzzzzzzzzzzzzzzz"}) {
            for (uint32_t type : kTypes) {
                char buffer[16];
                std::memset(buffer, 0x5A, sizeof(buffer));
                uint32_t result = CallMakeFileName(Code(original, 0x005A5F70, Mc::MakeFileNameStub), name, buffer, type);
                Record({result == reinterpret_cast<uint32_t>(buffer) ? 1u : result});
                for (int i = 0; i < 16; i += 4)
                    Record({*reinterpret_cast<uint32_t*>(buffer + i)});
            }
        }
        // TypeFromLetter: every byte (high bits set too)
        for (uint32_t letter = 0; letter < 256; ++letter)
            Record({CallLetter(Code(original, 0x005A60C0, Mc::TypeFromLetterStub), 0x12345600u | letter)});
        // IconSpaceRequired, TypeVersion, TypeFileSize, TypeFileName
        for (int platform : {0, 1, 4, 5, 6}) {
            GameVariable<int>(0x007CEFA8) = platform;
            for (uint32_t type : kTypes) {
                Record({CallEax(Code(original, 0x005A6180, Mc::IconSpaceRequiredStub), type)});
                Record({CallEax(Code(original, 0x005A6210, Mc::TypeVersionStub), type)});
                Record({CallEcx(Code(original, 0x005A7340, Mc::TypeFileSizeStub), type)});
            }
        }
        for (uint32_t type : kTypes) {
            std::memset(&GameVariable<char>(0x007CECE8), 0x41, 0x40);
            GameVariable<char>(0x007CECE8 + 0x40) = 0;
            uint32_t result = CallEaxEcx(Code(original, 0x005A62A0, Mc::TypeFileNameStub), type,
                                         reinterpret_cast<uint32_t>("skater one"));
            Record({result, Hash(&GameVariable<char>(0x007CECE8))});
        }
        // MakeFolderFileName
        for (uint32_t type : {Mc::kSavePark, Mc::kSaveCat, 0x777u}) {
            char buffer[64];
            std::memset(buffer, 0x33, sizeof(buffer));
            buffer[63] = 0;
            uint32_t result = CallEdiStack2(Code(original, 0x005A6390, Mc::MakeFolderFileNameStub), type,
                                            reinterpret_cast<uint32_t>("park name"), reinterpret_cast<uint32_t>(buffer));
            Record({result, Hash(buffer)});
        }
        // IsLater: each field deciding, equal dates, missing fields
        {
            const uint32_t names[6] = {0x447D8CC8, 0x7149EFF9, 0x1A5FD66F, 0x8FE1EEB1, 0x5F94B55C, 0xD029F619};
            const int base[6] = {2004, 10, 5, 13, 30, 45};
            for (int field = -1; field < 6; ++field) {
                for (int delta : {-1, 1, -100}) {
                    Params a;
                    Params b;
                    for (int i = 0; i < 6; ++i) {
                        a.Integer(names[i], base[i] + (i == field ? delta : 0));
                        b.Integer(names[i], base[i] - (i > field && field >= 0 ? 7 : 0));
                    }
                    void* code = Code(original, 0x005A63E0, Mc::IsLaterStub);
                    Record({CallEdiEsi(code, reinterpret_cast<uint32_t>(a.Struct()), reinterpret_cast<uint32_t>(b.Struct())) & 0xFF,
                            CallEdiEsi(code, reinterpret_cast<uint32_t>(b.Struct()), reinterpret_cast<uint32_t>(a.Struct())) & 0xFF});
                }
            }
            Params some;
            some.Integer(names[2], 3);
            Params none;
            void* code = Code(original, 0x005A63E0, Mc::IsLaterStub);
            Record({CallEdiEsi(code, reinterpret_cast<uint32_t>(some.Struct()), reinterpret_cast<uint32_t>(none.Struct())) & 0xFF,
                    CallEdiEsi(code, reinterpret_cast<uint32_t>(none.Struct()), reinterpret_cast<uint32_t>(some.Struct())) & 0xFF});
        }
        // RoundUpToCardBlock and SaveBlocks: every platform, sizes on and off block edges
        for (int platform = -1; platform <= 8; ++platform) {
            GameVariable<int>(0x007CEFA8) = platform;
            for (int size : {0, 1, 0x3FF, 0x400, 0x401, 0x1FFF, 0x2000, 0x4001, 123456, -5, -0x400}) {
                Record({CallEsi(Code(original, 0x005A6CD0, Mc::RoundUpToCardBlockStub), static_cast<uint32_t>(size))});
                for (uint32_t type : {Mc::kSavePark, Mc::kSaveCat, 0x55u}) {
                    int blocks = original ? Original<int (*)(uint32_t, int)>(0x005A8470)(type, size) : Mc::SaveBlocks(type, size);
                    Record({static_cast<uint32_t>(blocks)});
                }
            }
        }
        // ApplyGoalManagerParams: with and without GoalManagerParams
        {
            Params inner;
            inner.Integer(0x1111, 5);
            Params with;
            with.Integer(0x2222, 1).Structure(0xAC7C2B62, inner.Struct());
            Params without;
            without.Integer(0x2222, 1);
            for (Params* p : {&with, &without}) {
                if (original)
                    Original<void(__fastcall*)(CStruct*)>(0x005A6120)(p->Struct());
                else
                    Mc::ApplyGoalManagerParams(p->Struct());
                Record({0xFFFF});
            }
        }
        // ApplyCatAndCustomSkater: each skater index, both results
        GameVariable<uint32_t>(0x007CE478) = 0x5CA7E000;
        GameVariable<void*>(0x007CE4B4) = g_profiles;
        for (uint32_t index : {0u, 1u}) {
            for (uint32_t result : {0x100u, 0x101u}) {
                g_profileIndex = index;
                g_readCustomResult = result;
                uint32_t r = CallEdiStack1(Code(original, 0x005A6C80, Mc::ApplyCatAndCustomSkaterStub), 0x10AD0000, 0xC5000000);
                Record({r & 0xFF});
            }
        }
        // KeepLoadedContents: nothing kept, something kept; from the pool or the heap
        std::memset(g_newStruct, 0xEE, sizeof(g_newStruct));
        GameVariable<bool>(0x007CC979) = true;
        GameVariable<uint32_t>(0x007D1800) = 0x3A3A0000;
        for (int variant = 0; variant < 3; ++variant) {
            std::memset(reinterpret_cast<void*>(0x006F3614), 0, 16);
            if (variant != 1)
                GameVariable<uint32_t>(0x006F361C) = 0x0B0B0000;
            GameVariable<uint32_t>(0x007CECD4) = variant == 2 ? 0x0CEC0000u : 0u;
            const uint8_t data[8] = {};
            const uint8_t* end = original ? Original<const uint8_t* (*)(const uint8_t*, uint32_t)>(0x005A8EB0)(data, 0x7E7E0000u + variant)
                                          : Mc::KeepLoadedContents(data, 0x7E7E0000u + variant);
            Record({static_cast<uint32_t>(end - data), Mark(GameVariable<uint32_t>(0x007CECD4)), GameVariable<uint32_t>(0x007CECD8)});
            Record({*reinterpret_cast<uint32_t*>(g_newStruct), *reinterpret_cast<uint32_t*>(g_newStruct + 4)});
            std::memset(g_newStruct, 0xEE, sizeof(g_newStruct));
        }
        // the destructors: a node of a three-node ring, flag 0 and 1
        for (uint32_t which : {0u, 1u}) {
            for (unsigned int flags : {0u, 1u, 3u}) {
                Mem::ListNode nodes[3] = {};
                for (int i = 0; i < 3; ++i) {
                    nodes[i].vtable = reinterpret_cast<void**>(0x1000 + i);
                    nodes[i].next = &nodes[(i + 1) % 3];
                    nodes[i].prev = &nodes[(i + 2) % 3];
                }
                Mem::ListNode* result;
                auto* object = reinterpret_cast<Mc::ListDestructors*>(&nodes[1]);
                if (original)
                    result = Original<Mem::ListNode*(__thiscall*)(void*, unsigned int)>(which ? 0x005A8E80 : 0x005A7520)(&nodes[1], flags);
                else
                    result = which ? object->OtherDestroy(flags) : object->NodeDestroy(flags);
                Record({static_cast<uint32_t>(result - nodes)});
                for (Mem::ListNode& node : nodes)
                    Record({reinterpret_cast<uint32_t>(node.vtable), static_cast<uint32_t>(node.next - nodes),
                            static_cast<uint32_t>(node.prev - nodes)});
            }
        }
        // the save state commands
        Params none;
        for (int flag : {0, 1}) {
            GameVariable<uint8_t>(0x007CECD0) = static_cast<uint8_t>(flag);
            GameVariable<uint8_t>(0x007CECE0) = static_cast<uint8_t>(flag ^ 1);
            Call(original, 0x005A6550, Mc::SaveFailedDueToInsufficientSpace, none);
            Call(original, 0x005A6600, Mc::LoadedCustomSkater, none);
        }
        {
            const uint32_t flags[5] = {0x16B506C0, 0xDC7EA3CE, 0x33EC233F, 0xB39D7CC4, 0xC4E78E22};
            for (int mask = 0; mask < 32; ++mask) {
                Params p;
                p.Integer(0x3333, 1);
                for (int i = 0; i < 5; ++i)
                    if (mask & (1 << i))
                        p.Flag(flags[i]);
                GameVariable<uint32_t>(0x007CECDC) = 0xABCD;
                Call(original, 0x005A6560, Mc::SetSectionsToApplyWhenLoading, p);
                Record({GameVariable<uint32_t>(0x007CECDC)});
            }
        }
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(saved);

    Check(traces[0].size() > 1000, "save types: the original reached its calls");
    Compare(traces[0], traces[1], "save types and helpers (0x005A5F70-0x005A8EB0)");
}

// --- the summaries and the card commands ---

alignas(16) uint8_t g_summary[8];
alignas(16) uint8_t g_trickInfo[0x10];
alignas(16) uint8_t g_skate[0x40];
alignas(16) uint8_t g_career[0x600];
alignas(16) uint8_t g_editor[0x80];
alignas(16) uint8_t g_cardObject[0x10];
uint32_t g_preferencesStruct;
uint32_t g_goalManagerParams;
bool g_cardMissing;
std::vector<uint32_t> g_structs; // stand-in structures, recorded by their index

uint32_t StructIndex(uint32_t value)
{
    if (value == reinterpret_cast<uint32_t>(g_summary))
        return 0x5000;
    if (value == ScriptParams())
        return 0x5001;
    for (size_t i = 0; i < g_structs.size(); ++i)
        if (g_structs[i] == value)
            return 0x6000 + static_cast<uint32_t>(i);
    return value;
}

void StandInSummaryCalls(StandIns& s)
{
    using AddIntegerFn = void (CStruct::*)(uint32_t, int);
    s.At({0x00477B80, AddressOf(static_cast<AddIntegerFn>(&CStruct::AddInteger))}, 8, [](Regs& r) {
        Record({0x7B80, StructIndex(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    using AddChecksumFn = void (CStruct::*)(uint32_t, uint32_t);
    s.At({0x00477D40, AddressOf(static_cast<AddChecksumFn>(&CStruct::AddChecksum))}, 8, [](Regs& r) {
        Record({0x7D40, StructIndex(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    using AddStringFn = void (CStruct::*)(uint32_t, const char*);
    s.At({0x004779D0, AddressOf(static_cast<AddStringFn>(&CStruct::AddString))}, 8, [](Regs& r) {
        Record({0x79D0, StructIndex(r.ecx), r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
        return 0u;
    });
    using AddStringNamedFn = void (CStruct::*)(const char*, const char*);
    s.At({0x00477A90, AddressOf(static_cast<AddStringNamedFn>(&CStruct::AddString))}, 8, [](Regs& r) {
        Record({0x7A90, StructIndex(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])),
                Hash(reinterpret_cast<const char*>(r.args[1]))});
        return 0u;
    });
    using RemoveFn = void (CStruct::*)(const char*);
    s.At({0x00477770, AddressOf(static_cast<RemoveFn>(&CStruct::RemoveComponent))}, 4, [](Regs& r) {
        Record({0x7770, StructIndex(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    using AddStructureFn = void (CStruct::*)(const char*, CStruct*);
    s.At({0x00478770, AddressOf(static_cast<AddStructureFn>(&CStruct::AddStructure))}, 8, [](Regs& r) {
        Record({0x8770, StructIndex(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])), StructIndex(r.args[1])});
        return 0u;
    });
    // the park editor
    s.At({0x0058D640, AddressOf(&Ed::CParkEditor::GetMapInfo)}, 0x1C, [](Regs& r) {
        Record({0xD640, Mark(r.ecx) == r.ecx && r.ecx == reinterpret_cast<uint32_t>(g_editor) ? 1u : r.ecx, r.args[0]});
        for (int i = 1; i <= 6; ++i) {
            if (i == 3)
                *reinterpret_cast<uint16_t*>(r.args[i]) = static_cast<uint16_t>(0x8001 + r.args[0]);
            else
                *reinterpret_cast<uint32_t*>(r.args[i]) = 0x100 * i + (r.args[0] & 0xFF);
        }
        return 0u;
    });
    s.At({0x0058D680, AddressOf(&Ed::CParkEditor::WriteCompressedMapIfBuilt)}, 0, [](Regs& r) {
        Record({0xD680, r.ecx == reinterpret_cast<uint32_t>(g_editor) ? 1u : r.ecx});
        return 0u;
    });
    s.At({0x0058D690, AddressOf(&Ed::CParkEditor::GetCompressedMapBuffer)}, 4, [](Regs& r) {
        Record({0xD690, r.ecx == reinterpret_cast<uint32_t>(g_editor) ? 1u : r.ecx, r.args[0] & 0xFF});
        return 0x3A90000u;
    });
    s.At({0x0058DFA0, AddressOf(&Ed::CParkEditor::GetMaxPlayers)}, 0, [](Regs&) { Record({0xDFA0}); return 6u; });
    // the goal editor
    s.At({0x00462120, AddressOf(&Obj::CTracker::LookUpObject)}, 4, [](Regs& r) {
        Record({0x2120, r.ecx, r.args[0]});
        return 0x0B1E0000u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, r.ecx, r.args[0]});
        return 0x0C0E0000u;
    });
    s.At({0x004E7E00}, 0, [](Regs& r) { Record({0x7E00, r.ecx}); return 17u; });
    // the rest of the game's state
    s.At({0x00532F70, AddressOf(&GameNet::Manager::GetNetworkPreferences)}, 0, [](Regs& r) {
        Record({0x2F70, r.ecx});
        return g_preferencesStruct - 4;
    });
    s.At({0x005591B0, AddressOf(Game::GetGoalManager)}, 0, [](Regs&) { Record({0x91B0}); return reinterpret_cast<uint32_t>(g_goalManager); });
    s.At({0x004A2BA0}, 0, [](Regs& r) { Record({0x2BA0, Mark(r.ecx)}); return g_goalManagerParams; });
    s.At({0x0055C090}, 0, [](Regs& r) { Record({0xC090, Mark(r.ecx)}); return 0u; });
    s.At({0x005665C0, AddressOf(&Mdl::Skate::GetSkater)}, 4, [](Regs& r) {
        Record({0x65C0, r.ecx == reinterpret_cast<uint32_t>(g_skate) ? 1u : r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_skaters[0]);
    });
    // the card
    s.At({0x005BC730, AddressOf(&Mc::Manager::GetCard)}, 8, [](Regs& r) {
        Record({0xC730, r.ecx, r.args[0], r.args[1]});
        return g_cardMissing ? 0u : reinterpret_cast<uint32_t>(g_cardObject);
    });
    s.At({0x00450F70, AddressOf(&Pcm::PauseMusic)}, 0, [](Regs& r) { Record({0x0F70, r.args[0]}); return 0u; });
    s.At({0x00451010, AddressOf(&Pcm::PauseStream)}, 0, [](Regs& r) { Record({0x1010, r.args[0]}); return 0u; });
}

// WriteSummary: the contents in EAX, the summary (g_summary) in ESI, the type on the stack.
void CallSummary(void* code, CStruct* contents, uint32_t typeValue)
{
    uint32_t contentsWord = reinterpret_cast<uint32_t>(contents);
    uint32_t summaryWord = reinterpret_cast<uint32_t>(g_summary);
    __asm {
        push esi
        mov eax, contentsWord
        mov esi, summaryWord
        push typeValue
        call code
        add esp, 4
        pop esi
    }
}

// A hand-built structure the stand-ins know by index.
Params& NewParams(std::vector<Params*>& all)
{
    all.push_back(new Params);
    g_structs.push_back(reinterpret_cast<uint32_t>(all.back()->Struct()));
    return *all.back();
}

void TestSaveSummaries()
{
    std::vector<Saved> saved = {{0x007CEFA8, 4, {}}, {0x007CECD4, 8, {}}, {0x007CE478, 4, {}}, {0x007CE5D8, 4, {}},
                                {0x006F0FC0, 4, {}}, {0x007CCE14, 4, {}}, {0x007D1890, 4, {}}, {0x006F2338, 16, {}},
                                {0x006F3614, 16, {}}, {0x006F3654, 16, {}}, {0x006F3624, 4, {}}};
    SaveAll(saved);
    UseEmptySymbolTable();
    auto* standIns = new StandIns;
    StandInSummaryCalls(*standIns);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        g_structs.clear();
        std::vector<Params*> all;
        GameVariable<void*>(0x007CE478) = g_skate;
        GameVariable<void*>(0x007CE5D8) = g_editor;
        GameVariable<uint32_t>(0x006F0FC0) = 0x7A000000;
        GameVariable<uint32_t>(0x007CCE14) = 0x6E700000;
        GameVariable<uint32_t>(0x007D1890) = 0x3C000000;
        // the game's state: the skater's trick info, the career, the network preferences, the goal manager
        Params& trickInfo = NewParams(all);
        trickInfo.String(0xA1DC81F9, "Live Skater");
        *reinterpret_cast<uint32_t*>(g_trickInfo + 4) = reinterpret_cast<uint32_t>(trickInfo.Struct());
        *reinterpret_cast<uint32_t*>(g_skaters[0] + 0x17C) = reinterpret_cast<uint32_t>(g_trickInfo);
        *reinterpret_cast<uint32_t*>(g_skate + 0x20) = reinterpret_cast<uint32_t>(g_career);
        *reinterpret_cast<uint16_t*>(g_career + 0x5D8) = 0x9123;
        *reinterpret_cast<uint16_t*>(g_career + 0x5DA) = 0x8456;
        g_career[0x5EE] = 0xC7;
        g_career[0x5EF] = 0x82;
        Params& networkId = NewParams(all);
        networkId.String(0x96875C0F, "player one");
        Params& preferences = NewParams(all);
        preferences.Structure(0xCBED746E, networkId.Struct());
        g_preferencesStruct = reinterpret_cast<uint32_t>(preferences.Struct());
        Params& goalManagerParams = NewParams(all);
        goalManagerParams.Integer(0x1234, 1);
        g_goalManagerParams = reinterpret_cast<uint32_t>(goalManagerParams.Struct());

        // saved contents
        Params& otherParams = NewParams(all);
        otherParams.String(0xA1DC81F9, "Saved Cat");
        Params& cat = NewParams(all);
        cat.Structure(0x4137CB14, otherParams.Struct());
        static Script::CArray goalsArray;
        goalsArray.m_size = 3;
        Params& createdGoals = NewParams(all);
        createdGoals.Array(0x38DBE1D0, &goalsArray);
        Params& noGoals = NewParams(all);
        noGoals.Integer(0x1, 2);
        static Script::CArray mapArray;
        static uint32_t mapElements[4] = {1, 2, 3, 4};
        mapArray.m_size = 4;
        mapArray.m_type = Script::ESYMBOLTYPE_INTEGER;
        mapArray.m_elements = mapElements;
        Params& parkGoals = NewParams(all);
        parkGoals.Array(0x38DBE1D0, &goalsArray);
        Params& parkGoalsEmpty = NewParams(all);
        parkGoalsEmpty.Integer(0x2, 3);
        Params& park = NewParams(all);
        park.Structure(0xD8EB825E, parkGoals.Struct()).Integer(0xB7E39B53, 4).Array(0x337C5289, &mapArray);
        Params& parkNoGoals = NewParams(all);
        parkNoGoals.Structure(0xD8EB825E, parkGoalsEmpty.Struct()).Array(0x337C5289, &mapArray);
        Params& parkNothing = NewParams(all);
        parkNothing.Array(0x337C5289, &mapArray);
        // OptionsAndPros: the story's goal manager, the career (all four, the first only, none), the custom skater
        Params& gmInner = NewParams(all);
        gmInner.Integer(0x77, 1);
        Params& gm = NewParams(all);
        gm.Structure(0x23D4170A, gmInner.Struct());
        Params& story = NewParams(all);
        story.Structure(0xAC7C2B62, gm.Struct());
        Params& info = NewParams(all);
        info.Integer(0x3F813177, 1).String(0x2AB66CB8, "Custom Name");
        Params& infoEmpty = NewParams(all);
        infoEmpty.Integer(0x99, 0);
        Params& custom = NewParams(all);
        custom.Structure(0x3476CEA8, info.Struct());
        Params& customEmpty = NewParams(all);
        customEmpty.Structure(0x3476CEA8, infoEmpty.Struct());
        Params& customSkater = NewParams(all);
        customSkater.Structure(0x0A7BE964, custom.Struct());
        Params& customSkaterEmpty = NewParams(all);
        customSkaterEmpty.Structure(0x0A7BE964, customEmpty.Struct());
        Params& careerAll = NewParams(all);
        careerAll.Integer(0x6C1A5E39, 11).Integer(0x7A6BF912, 22).Integer(0x0444419A, 33).Integer(0x95947B61, 44);
        Params& careerFirst = NewParams(all);
        careerFirst.Integer(0x6C1A5E39, 55).Integer(0x0444419A, 66);
        Params& careerNone = NewParams(all);
        careerNone.Integer(0x7A6BF912, 77);
        std::vector<Params*> optionSaves;
        for (Params* careerParams : {&careerAll, &careerFirst, &careerNone, static_cast<Params*>(nullptr)}) {
            Params& global = NewParams(all);
            if (careerParams)
                global.Structure(0x4DA4937B, careerParams->Struct());
            Params& options = NewParams(all);
            options.Structure(0x14A9FBC7, story.Struct());
            options.Structure(0xF55CBD13, global.Struct());
            options.Structure(0x12BFAC82, careerParams ? customSkater.Struct() : customSkaterEmpty.Struct());
            optionSaves.push_back(&options);
        }
        {
            Params& options = NewParams(all); // no GlobalInfo
            options.Structure(0x14A9FBC7, story.Struct()).Structure(0x12BFAC82, customSkater.Struct());
            optionSaves.push_back(&options);
        }
        Params& network = NewParams(all);
        network.Integer(0x3, 4);

        void* summaryCode = Code(original, 0x005A6610, Mc::WriteSummaryStub);
        auto summarize = [&](CStruct* contents, uint32_t type) {
            std::memset(g_summary, 0, sizeof(g_summary));
            CallSummary(summaryCode, contents, type);
            Record({0xEEEE});
        };
        for (uint32_t type : kTypes)
            summarize(nullptr, type);
        summarize(cat.Struct(), Mc::kSaveCat);
        summarize(createdGoals.Struct(), Mc::kSaveCreatedGoals);
        summarize(noGoals.Struct(), Mc::kSaveCreatedGoals);
        summarize(park.Struct(), Mc::kSavePark);
        summarize(parkNoGoals.Struct(), Mc::kSavePark);
        summarize(parkNothing.Struct(), Mc::kSavePark);
        for (Params* options : optionSaves)
            summarize(options->Struct(), Mc::kSaveOptionsAndPros);
        summarize(network.Struct(), Mc::kSaveNetworkSettings);
        for (uint32_t type : {Mc::kSaveCas, Mc::kSaveReplay, 0x1u})
            summarize(network.Struct(), type);

        // GetSummaryInfo: now (each type), the kept contents (VaultData)
        for (uint32_t type : {Mc::kSaveOptionsAndPros, Mc::kSaveCat, Mc::kSavePark}) {
            Params p;
            p.Checksum(0, type);
            Call(original, 0x005A72A0, Mc::GetSummaryInfo, p);
            Params q;
            q.Checksum(0, type).Integer(0xC78DABF6, 0);
            Call(original, 0x005A72A0, Mc::GetSummaryInfo, q);
        }
        GameVariable<uint32_t>(0x007CECD4) = reinterpret_cast<uint32_t>(cat.Struct());
        GameVariable<uint32_t>(0x007CECD8) = Mc::kSaveCat;
        {
            Params p;
            p.Checksum(0, Mc::kSavePark).Integer(0xC78DABF6, 2);
            Call(original, 0x005A72A0, Mc::GetSummaryInfo, p);
            Call(original, 0x005A7430, Mc::GetMemCardDataForUpload, p);
            Call(original, 0x005A7450, Mc::GetMaxTHPS4FilesAllowed, p);
        }

        // GetMostRecentSave: types, dates, bad ones per platform, no array
        {
            const uint32_t dateNames[6] = {0x447D8CC8, 0x7149EFF9, 0x1A5FD66F, 0x8FE1EEB1, 0x5F94B55C, 0xD029F619};
            struct Entry {
                uint32_t fileType;
                int year;
                int minutes;
                const char* flag;
            };
            const Entry entries[] = {{Mc::kSavePark, 2003, 5, nullptr},  {0, 2004, 1, nullptr},
                                     {Mc::kSaveCat, 2009, 0, nullptr},   {Mc::kSavePark, 2004, 9, "BadVersion"},
                                     {Mc::kSavePark, 2004, 7, nullptr},  {Mc::kSavePark, 2005, 0, "Corrupt"},
                                     {Mc::kSavePark, 2004, 7, nullptr}};
            static Script::CArray saves;
            static uint32_t elements[7];
            saves.m_size = 7;
            saves.m_type = Script::ESYMBOLTYPE_STRUCTURE;
            saves.m_elements = elements;
            for (int i = 0; i < 7; ++i) {
                Params& e = NewParams(all);
                if (entries[i].fileType)
                    e.Checksum(Crc::GenerateCRCFromString("file_type"), entries[i].fileType);
                for (int f = 0; f < 6; ++f)
                    e.Integer(dateNames[f], f == 0 ? entries[i].year : f == 4 ? entries[i].minutes : 1);
                if (entries[i].flag)
                    e.Flag(Crc::GenerateCRCFromString(entries[i].flag));
                elements[i] = reinterpret_cast<uint32_t>(e.Struct());
            }
            for (int platform : {1, 4, 5}) {
                GameVariable<int>(0x007CEFA8) = platform;
                for (uint32_t type : {Mc::kSavePark, Mc::kSaveCat, Mc::kSaveCreatedGraphic, 0u}) {
                    Params p;
                    p.Array(0, &saves).Checksum(0, type);
                    Call(original, 0x005A6D30, Mc::GetMostRecentSave, p);
                }
            }
            Params none;
            Call(original, 0x005A6D30, Mc::GetMostRecentSave, none);
            saves.m_size = 0;
            Params empty;
            empty.Array(0, &saves);
            Call(original, 0x005A6D30, Mc::GetMostRecentSave, empty);
        }

        // the card commands, with and without a card
        for (bool missing : {false, true}) {
            g_cardMissing = missing;
            Params none;
            Call(original, 0x005A7210, Mc::FormatCard, none);
            Call(original, 0x005A7260, Mc::CardIsInSlot, none);
            Call(original, 0x005A7280, Mc::CardIsFormatted, none);
        }
        // the temporary pools: all there, one missing (each), switched back
        for (int missing = -1; missing < 3; ++missing) {
            for (int start : {0, 1}) {
                for (uint32_t pools : {0x006F2338u, 0x006F3614u, 0x006F3654u}) {
                    std::memset(reinterpret_cast<void*>(pools), 0, 16);
                    GameVariable<int>(pools) = start;
                    GameVariable<uint32_t>(pools + 8) = 0x900;
                    GameVariable<uint32_t>(pools + 12) = 0x901;
                }
                if (missing >= 0) {
                    uint32_t pools = missing == 0 ? 0x006F2338u : missing == 1 ? 0x006F3614u : 0x006F3654u;
                    GameVariable<uint32_t>(pools + 12) = 0;
                }
                Params none;
                Call(original, 0x005A7470, Mc::SwitchToTempPoolsIfTheyExist, none);
                for (uint32_t pools : {0x006F2338u, 0x006F3614u, 0x006F3654u})
                    Record({GameVariable<uint32_t>(pools)});
                Call(original, 0x005A74F0, Mc::SwitchToRegularPools, none);
                for (uint32_t pools : {0x006F2338u, 0x006F3614u, 0x006F3654u})
                    Record({GameVariable<uint32_t>(pools)});
            }
        }
        // back to the regular pools when only one pair isn't on its first pool
        for (int which = 0; which < 3; ++which) {
            const uint32_t pairs[3] = {0x006F2338u, 0x006F3614u, 0x006F3654u};
            for (uint32_t pools : pairs)
                GameVariable<int>(pools) = 0;
            GameVariable<int>(pairs[which]) = 1;
            Params none;
            Call(original, 0x005A74F0, Mc::SwitchToRegularPools, none);
            for (uint32_t pools : pairs)
                Record({GameVariable<uint32_t>(pools)});
        }
        for (uint32_t& word : g_trace)
            word = StructIndex(word);
        for (Params* p : all)
            delete p;
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(saved);
    Check(traces[0].size() > 500, "save summaries: the original reached its calls");
    Compare(traces[0], traces[1], "save summaries and card commands (0x005A6610-0x005A74F0)");
}

} // namespace

SELFTEST(Early, TestSaveTypes, RemoveStandIns);
SELFTEST(Early, TestSaveSummaries, RemoveStandIns);
