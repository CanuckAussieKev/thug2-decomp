// Self-tests for what a save holds and how it is put back (save_contents.cpp): each original against ours, every
// call into the rest of the game stood in for by recording code, new structures and arrays from one arena per run.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/skate/skate_module.h"
#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/pool.h"
#include "game/ed/park_editor.h"
#include "game/mc/save_contents.h"
#include "game/mc/save_files_cfuncs.h"
#include "game/script/struct_buffer.h"
#include "game/mc/save_types.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/object.h"
#include "game/obj/tracker.h"
#include "game/pcm/music.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/skate/career.h"
#include "game/skate/profile_manager.h"
#include "game/skate/records.h"
#include "game/skater/skater.h"
#include "hook/cfunc_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using namespace CFuncTests;
using Script::CArray;
using Script::CStruct;

constexpr unsigned int kArenaSize = 0x10000;
ArenaPair<kArenaSize> g_contentsStorage;
uint8_t (*const g_contentsArenas)[kArenaSize] = g_contentsStorage.Get();
uint8_t* g_arena;
uint32_t g_arenaUsed;

alignas(16) uint8_t g_skate[0x400];
alignas(16) uint8_t g_skater[0x200];
alignas(16) uint8_t g_trickInfo[0x10];
alignas(16) uint8_t g_profile[0x80];
alignas(16) uint8_t g_profiles[0x40];
uint32_t g_profileIndex;
bool g_isPro;
float g_mainVolume = 0.625f;
float g_musicVolOutsideBox = 0.375f;
uint32_t g_randomOrder;
bool g_noLocalSkater;
bool g_symbolFound;
alignas(16) uint8_t g_symbol[0x14];
std::vector<uint32_t> g_known; // structures recorded by index

uint32_t Relocate(uint32_t word) { return ArenaWord(g_contentsArenas, kArenaSize, word); }
uint32_t Known(uint32_t value)
{
    for (size_t i = 0; i < g_known.size(); ++i)
        if (g_known[i] == value)
            return 0x7000 + static_cast<uint32_t>(i);
    if (value == ScriptParams())
        return 0x5001;
    return Relocate(value);
}
uint32_t Bump(uint32_t size)
{
    uint32_t at = (g_arenaUsed + 15) & ~15u;
    g_arenaUsed = at + size;
    std::memset(g_arena + at, 0xCD, size);
    return reinterpret_cast<uint32_t>(g_arena + at);
}

// `fld dword [value]; ret` over a function returning a float.
std::vector<uint8_t> LoadFloat(const float* value)
{
    std::vector<uint8_t> bytes = {0xD9, 0x05, 0, 0, 0, 0, 0xC3};
    uint32_t address = reinterpret_cast<uint32_t>(value);
    std::memcpy(&bytes[2], &address, 4);
    return bytes;
}

void StandInCalls(StandIns& s)
{
    // new structures and arrays
    s.At({0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, [](Regs& r) {
        Record({0xA3D0, r.ecx});
        return Bump(0x10);
    });
    // the structure methods
    using AddPointerFn = void (CStruct::*)(uint32_t, CStruct*);
    s.At({0x004781B0, AddressOf(static_cast<AddPointerFn>(&CStruct::AddStructurePointer))}, 8, [](Regs& r) {
        Record({0x81B0, Known(r.ecx), r.args[0], Known(r.args[1])});
        return 0u;
    });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Record({0x8540, Known(r.ecx), Known(r.args[0])});
        return 0u;
    });
    using AddFloatFn = void (CStruct::*)(uint32_t, float);
    s.At({0x00477C60, AddressOf(static_cast<AddFloatFn>(&CStruct::AddFloat))}, 8, [](Regs& r) {
        Record({0x7C60, Known(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    using AddIntegerFn = void (CStruct::*)(uint32_t, int);
    s.At({0x00477B80, AddressOf(static_cast<AddIntegerFn>(&CStruct::AddInteger))}, 8, [](Regs& r) {
        Record({0x7B80, Known(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    using AddChecksumFn = void (CStruct::*)(uint32_t, uint32_t);
    s.At({0x00477D40, AddressOf(static_cast<AddChecksumFn>(&CStruct::AddChecksum))}, 8, [](Regs& r) {
        Record({0x7D40, Known(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    using AddArrayPointerFn = void (CStruct::*)(uint32_t, CArray*);
    s.At({0x004780D0, AddressOf(static_cast<AddArrayPointerFn>(&CStruct::AddArrayPointer))}, 8, [](Regs& r) {
        Record({0x80D0, Known(r.ecx), r.args[0], Known(r.args[1])});
        return 0u;
    });
    using AddStructureFn = void (CStruct::*)(uint32_t, CStruct*);
    s.At({0x00478670, AddressOf(static_cast<AddStructureFn>(&CStruct::AddStructure))}, 8, [](Regs& r) {
        Record({0x8670, Known(r.ecx), r.args[0], Known(r.args[1])});
        return 0u;
    });
    using AddStructureNamedFn = void (CStruct::*)(const char*, CStruct*);
    s.At({0x00478770, AddressOf(static_cast<AddStructureNamedFn>(&CStruct::AddStructure))}, 8, [](Regs& r) {
        Record({0x8770, Known(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])), Known(r.args[1])});
        return 0u;
    });
    using AddArrayNamedFn = void (CStruct::*)(const char*, CArray*);
    s.At({0x00478C50, AddressOf(static_cast<AddArrayNamedFn>(&CStruct::AddArray))}, 8, [](Regs& r) {
        Record({0x8C50, Known(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])), Known(r.args[1])});
        return 0u;
    });
    using AddArrayFn = void (CStruct::*)(uint32_t, CArray*);
    s.At({0x00478B00, AddressOf(static_cast<AddArrayFn>(&CStruct::AddArray))}, 8, [](Regs& r) {
        Record({0x8B00, Known(r.ecx), r.args[0], Known(r.args[1])});
        return 0u;
    });
    s.At({0x0046CC50, AddressOf(&CArray::SetSizeAndType)}, 8, [](Regs& r) {
        Record({0xCC50, Known(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x0046CB10, AddressOf(&CArray::SetElement)}, 8, [](Regs& r) {
        Record({0xCB10, Known(r.ecx), r.args[0], Known(r.args[1])});
        return 0u;
    });
    s.At({0x00477130}, 0, [](Regs& r) { Record({0x7090, Known(r.ecx)}); return 0u; }); // (a jump to Clear)
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) { Record({0x9070, Known(r.args[0])}); return 0u; });
    using GetSymbolValueFn = uint32_t (*)(const char*);
    s.At({0x00478FB0, AddressOf(static_cast<GetSymbolValueFn>(&Script::GetSymbolValue))}, 0, [](Regs& r) {
        Record({0x8FB0, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0x50D7AC00u;
    });
    using GetArrayFn = CArray* (*)(uint32_t);
    s.At({0x00479070, AddressOf(static_cast<GetArrayFn>(&Script::GetArray))}, 0, [](Regs& r) {
        Record({0x9070A, r.args[0]});
        return 0x0A770000u;
    });
    s.At({0x00478CC0, AddressOf(&Script::FindSymbol)}, 0, [](Regs& r) {
        // (only current_soundtrack's look-up: the structure getters look names up too, ours through other paths)
        if (r.args[0] != 0xE1F7C4AE)
            return 0u;
        Record({0x8CC0, r.args[0]});
        return g_symbolFound ? reinterpret_cast<uint32_t>(g_symbol) : 0u;
    });
    s.At({0x00478A60, AddressOf(&Script::CopyArray)}, 0, [](Regs& r) {
        Record({0x8A60, Known(r.args[0]), Known(r.args[1])});
        return 0u;
    });
    using RunNamedFn = void (*)(const char*, CStruct*, Obj::CObject*, bool);
    s.At({0x004758F0, AddressOf(static_cast<RunNamedFn>(&Script::RunScript))}, 0, [](Regs& r) {
        Record({0x58F0, Hash(reinterpret_cast<const char*>(r.args[0])), Known(r.args[1]), r.args[2], r.args[3] & 0xFF});
        return 0u;
    });
    using RunFn = void (*)(uint32_t, CStruct*, Obj::CObject*, bool, const char*);
    s.At({0x00475790, AddressOf(static_cast<RunFn>(&Script::RunScript))}, 0, [](Regs& r) {
        Record({0x5790, r.args[0], Known(r.args[1]), r.args[2], r.args[3] & 0xFF, r.args[4]});
        return 0u;
    });
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0x1C30, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    // sound and music
    s.Bytes(0x0047AFD0, LoadFloat(&g_mainVolume));
    s.Bytes(AddressOf(&Sfx::GetMainVolume), LoadFloat(&g_mainVolume));
    using GetFloatFn = float (*)(uint32_t);
    s.Bytes(0x00478F00, LoadFloat(&g_musicVolOutsideBox));
    s.Bytes(AddressOf(static_cast<GetFloatFn>(&Script::GetFloat)), LoadFloat(&g_musicVolOutsideBox));
    s.At({0x00450480, AddressOf(&Pcm::GetTracksOff)}, 0, [](Regs& r) {
        Record({0x0480});
        *reinterpret_cast<uint64_t*>(r.args[0]) = 0x8123456700ABCDEFull;
        *reinterpret_cast<uint64_t*>(r.args[1]) = 0x0FEDCBA987654321ull;
        return 0u;
    });
    s.At({0x00450460, AddressOf(&Pcm::RandomTrackOrder)}, 0, [](Regs&) { Record({0x0460}); return g_randomOrder; });
    s.At({0x004504B0, AddressOf(&Pcm::SetTracksOff)}, 0, [](Regs& r) {
        Record({0x04B0, r.args[0], r.args[1], r.args[2], r.args[3]});
        return 0u;
    });
    s.At({0x00451380, AddressOf(&Pcm::SetRandomMode)}, 0, [](Regs& r) { Record({0x1380, r.args[0]}); return 0u; });
    s.At({0x004510B0, AddressOf(&Pcm::SetMusicVolume)}, 0, [](Regs& r) { Record({0x10B0, r.args[0]}); return 0u; });
    s.At({0x0047B5D0, AddressOf(&Sfx::CSfxManager::SetMainVolume)}, 4, [](Regs& r) {
        Record({0xB5D0, r.ecx, r.args[0]});
        return 0u;
    });
    // preferences, career, records, pros, profiles
    s.At({0x00564D80}, 0, [](Regs& r) { Record({0x4D80, Known(r.ecx)}); return 0x5B11E000u; });
    s.At({0x00532F80, AddressOf(&GameNet::Manager::GetTauntPreferences)}, 0, [](Regs& r) {
        Record({0x2F80, r.ecx});
        return 0x7A0E0000u;
    });
    s.At({0x00532F70, AddressOf(&GameNet::Manager::GetNetworkPreferences)}, 0, [](Regs& r) {
        Record({0x2F70, r.ecx});
        return 0x0E7E0000u;
    });
    s.At({0x0046C500, AddressOf(&Prefs::Preferences::SetPreferences)}, 4, [](Regs& r) {
        Record({0xC500, r.ecx, Known(r.args[0])});
        return 0u;
    });
    s.At({0x00585550, AddressOf(&Obj::CSkaterCareer::WriteToStructure)}, 4, [](Regs& r) {
        Record({0x5550, r.ecx, Known(r.args[0])});
        return 0u;
    });
    s.At({0x00584D00, AddressOf(&Obj::CSkaterCareer::ReadFromStructure)}, 4, [](Regs& r) {
        Record({0x4D00, r.ecx, Known(r.args[0])});
        return 0u;
    });
    s.At({0x0057FC50, AddressOf(&Records::CGameRecords::WriteToStructure)}, 4, [](Regs& r) {
        Record({0xFC50, r.ecx, Known(r.args[0])});
        return 0u;
    });
    s.At({0x0057F440, AddressOf(&Records::CGameRecords::ReadFromStructure)}, 4, [](Regs& r) {
        Record({0xF440, r.ecx, Known(r.args[0])});
        return 0u;
    });
    s.At({0x0057B840, AddressOf(&Obj::CPlayerProfileManager::WritePros)}, 4, [](Regs& r) {
        Record({0xB840, Known(r.ecx), Known(r.args[0])});
        return 0u;
    });
    s.At({0x0057BAA0, AddressOf(&Obj::CPlayerProfileManager::ReadPros)}, 4, [](Regs& r) {
        Record({0xBAA0, Known(r.ecx), Known(r.args[0])});
        return 0u;
    });
    s.At({0x0057B920, AddressOf(&Obj::CPlayerProfileManager::WriteCustomSkaters)}, 4, [](Regs& r) {
        Record({0xB920, Known(r.ecx), Known(r.args[0])});
        return 0u;
    });
    s.At({0x0057BD70, AddressOf(&Obj::CPlayerProfileManager::ReadCustomSkater)}, 8, [](Regs& r) {
        Record({0xBD70, Known(r.ecx), Known(r.args[0]), r.args[1] & 0xFF});
        return 1u;
    });
    s.At({0x00553E90}, 0, [](Regs& r) { Record({0x3E90, Known(r.ecx)}); return g_profileIndex; });
    s.At({0x00564F80}, 4, [](Regs& r) {
        Record({0x4F80, Known(r.ecx), r.args[0]});
        return reinterpret_cast<uint32_t>(g_profile);
    });
    s.At({0x00586170, AddressOf(&Obj::CSkaterProfile::IsPro)}, 0, [](Regs& r) {
        Record({0x6170, Known(r.ecx)});
        return g_isPro ? 1u : 0u;
    });
    s.At({0x005861F0, AddressOf(&Obj::CSkaterProfile::SetStringF36C1878)}, 4, [](Regs& r) {
        Record({0x61F0, Known(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    for (uint32_t address : {0x00567C30u, 0x00567C00u, 0x00567BD0u}) {
        s.At({address}, 8, [address](Regs& r) {
            Record({address, Known(r.ecx), r.args[0], r.args[1] & 0xFF});
            return 0u;
        });
    }
    // the skaters, the goal editor, the park editor, the goal manager
    s.At({0x005665C0, AddressOf(&Mdl::Skate::GetSkater)}, 4, [](Regs& r) {
        Record({0x65C0, Known(r.ecx), r.args[0]});
        return reinterpret_cast<uint32_t>(g_skater);
    });
    s.At({0x00566520}, 0, [](Regs& r) {
        Record({0x6520, Known(r.ecx)});
        return g_noLocalSkater ? 0u : reinterpret_cast<uint32_t>(g_skater);
    });
    s.At({0x00580060, AddressOf(&Obj::CSkater::LoadCATInfo)}, 4, [](Regs& r) { Record({0x0060, Known(r.ecx), Known(r.args[0])}); return 0u; });
    s.At({0x00580150, AddressOf(&Obj::CSkater::LoadList44B66A9F)}, 4, [](Regs& r) { Record({0x0150, Known(r.ecx), Known(r.args[0])}); return 0u; });
    s.At({0x00580210, AddressOf(&Obj::CSkater::ReadChapterInfo)}, 4, [](Regs& r) { Record({0x0210, Known(r.ecx), Known(r.args[0])}); return 0u; });
    s.At({0x005825E0, AddressOf(&Obj::CSkater::WriteCATInfo)}, 4, [](Regs& r) { Record({0x25E0, Known(r.ecx), Known(r.args[0])}); return 0u; });
    s.At({0x00582740, AddressOf(&Obj::CSkater::WriteList44B66A9F)}, 4, [](Regs& r) { Record({0x2740, Known(r.ecx), Known(r.args[0])}); return 0u; });
    s.At({0x005801B0, AddressOf(&Obj::CSkater::WriteChapterInfo)}, 4, [](Regs& r) { Record({0x01B0, Known(r.ecx), Known(r.args[0])}); return 0u; });
    s.At({0x00462120, AddressOf(&Obj::CTracker::LookUpObject)}, 4, [](Regs& r) {
        Record({0x2120, r.ecx, r.args[0]});
        return 0x0B1E0000u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, r.ecx, r.args[0]});
        return 0x0C0E0000u;
    });
    s.At({0x004EB850}, 4, [](Regs& r) { Record({0xB850, r.ecx, Known(r.args[0])}); return 0u; });
    s.At({0x004EA470}, 8, [](Regs& r) { Record({0xA470, r.ecx, Known(r.args[0]), r.args[1]}); return 0u; });
    s.At({0x0058D600, AddressOf(&Ed::CParkEditor::WriteIntoStructure)}, 4, [](Regs& r) {
        Record({0xD600, r.ecx, Known(r.args[0])});
        return 0u;
    });
    s.At({0x0058D620, AddressOf(&Ed::CParkEditor::ReadFromStructure)}, 8, [](Regs& r) {
        Record({0xD620, r.ecx, Known(r.args[0]), r.args[1] & 0xFF});
        return 0u;
    });
    s.At({0x005591B0}, 0, [](Regs&) { Record({0x91B0}); return 0x60A10000u; });
    s.At({0x004A2BA0}, 0, [](Regs& r) { Record({0x2BA0, r.ecx}); return 0x60A1A000u; });
    s.At({0x00557E70}, 4, [](Regs& r) { Record({0x7E70, r.ecx, r.args[0]}); return 0u; });
    s.At({0x00558A00}, 0, [](Regs& r) { Record({0x8A00, r.ecx}); return 0u; });
    s.At({0x00477090, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) { Record({0x7090, Known(r.ecx)}); return 0u; });
    // GetSaveInfo's
    s.At({0x0047A070, AddressOf(&Script::CalculateBufferSize)}, 0, [](Regs& r) {
        Record({0xA070, Known(r.args[0]), r.args[1]});
        return 0x100u + (g_arenaUsed & 0xFF);
    });
    using GetIntegerFn = int (*)(uint32_t);
    s.At({0x00478F60, AddressOf(static_cast<GetIntegerFn>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x8F60, r.args[0]});
        return 0x2345u;
    });
    using AddStringNamedFn = void (CStruct::*)(const char*, const char*);
    s.At({0x00477A90, AddressOf(static_cast<AddStringNamedFn>(&CStruct::AddString))}, 8, [](Regs& r) {
        Record({0x7A90, Known(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])), Hash(reinterpret_cast<const char*>(r.args[1]))});
        return 0u;
    });
}

void CallWriteContents(void* code, CStruct* contents, uint32_t typeValue)
{
    __asm {
        push ebx
        mov ebx, contents
        push typeValue
        call code
        add esp, 4
        pop ebx
    }
}
void CallApply(void* code, uint32_t typeValue, CStruct* contents, void* script, CStruct* params)
{
    __asm {
        push esi
        mov eax, typeValue
        mov esi, contents
        push params
        push script
        call code
        add esp, 8
        pop esi
    }
}

Params& NewParams(std::vector<Params*>& all)
{
    all.push_back(new Params);
    g_known.push_back(reinterpret_cast<uint32_t>(all.back()->Struct()));
    return *all.back();
}

void TestSaveContents()
{
    std::vector<Saved> saved = {{0x007CEFA8, 4, {}}, {0x007CE478, 4, {}}, {0x007CE4B4, 4, {}}, {0x007CE4BC, 4, {}},
                                {0x007CCE14, 4, {}}, {0x006F85A8, 4, {}}, {0x006F0FC0, 4, {}}, {0x007CE5D8, 4, {}},
                                {0x006F3614, 16, {}}, {0x006F22B4, 16, {}}, {0x007CECDC, 8, {}}, {0x006F3624, 4, {}}};
    SaveAll(saved);
    UseEmptySymbolTable();
    auto* standIns = new StandIns;
    StandInCalls(*standIns);
    const uint32_t types[] = {Mc::kSaveCreatedGoals, Mc::kSaveCat,           Mc::kSavePark,  Mc::kSaveNetworkSettings,
                              Mc::kSaveOptionsAndPros, Mc::kSaveCreatedGraphic, Mc::kSaveCas, 0x1234u};

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        g_known.clear();
        std::vector<Params*> all;
        g_arena = g_contentsArenas[run];
        g_arenaUsed = 0;
        std::memset(g_skate, 0, sizeof(g_skate));
        *reinterpret_cast<uint32_t*>(g_skate + 0x20) = 0xCA2EE200;
        for (int i = 0; i < 8; ++i) {
            g_skate[0x394 + 8 * i] = static_cast<uint8_t>(i & 1);
            g_skate[0x395 + 8 * i] = static_cast<uint8_t>((i >> 1) & 1);
            g_skate[0x396 + 8 * i] = static_cast<uint8_t>((i >> 2) & 1);
        }
        GameVariable<void*>(0x007CE478) = g_skate;
        GameVariable<void*>(0x007CE4B4) = g_profiles;
        GameVariable<uint32_t>(0x007CE4BC) = 0x2EC02D00;
        GameVariable<uint32_t>(0x007CCE14) = 0x6E700000;
        GameVariable<uint32_t>(0x006F85A8) = 0x5F800000;
        GameVariable<uint32_t>(0x006F0FC0) = 0x7A000000;
        GameVariable<uint32_t>(0x007CE5D8) = 0x9A2C0000;
        std::memset(reinterpret_cast<void*>(0x006F3614), 0, 16);
        std::memset(reinterpret_cast<void*>(0x006F22B4), 0, 16);
        GameVariable<uint32_t>(0x006F361C) = 0x0B0B0000; // the structure and array pools there
        GameVariable<uint32_t>(0x006F22BC) = 0x0B0C0000;
        // the skater's trick info: +4 a structure, +8 and +0xC arrays
        Params& skaterInfo = NewParams(all);
        skaterInfo.Integer(0x1, 1);
        *reinterpret_cast<uint32_t*>(g_trickInfo + 4) = reinterpret_cast<uint32_t>(skaterInfo.Struct());
        *reinterpret_cast<uint32_t*>(g_trickInfo + 8) = 0x0A880000;
        *reinterpret_cast<uint32_t*>(g_trickInfo + 0xC) = 0x0A8C0000;
        *reinterpret_cast<uint32_t*>(g_skater + 0x17C) = reinterpret_cast<uint32_t>(g_trickInfo);

        // WriteGlobalInfo and WriteSaveContents for every type, on the PC and another platform
        for (int platform : {4, 2}) {
            GameVariable<int>(0x007CEFA8) = platform;
            for (uint32_t random : {0u, 3u}) {
                g_randomOrder = random;
                Params& global = NewParams(all);
                if (original)
                    Original<void (*)(CStruct*)>(0x005A7560)(global.Struct());
                else
                    Mc::WriteGlobalInfo(global.Struct());
                Record({0xEEEE});
            }
            for (uint32_t type : types) {
                Params& contents = NewParams(all);
                CallWriteContents(original ? OriginalFunction(0x005A79A0) : reinterpret_cast<void*>(&Mc::WriteSaveContentsStub),
                                  contents.Struct(), type);
                Record({0xEEEF});
            }
        }

        // ReadGlobalInfo: a full save, one without the music volume / track flags / controllers / career
        {
            const uint32_t kSound = 0x89EB9738;
            Params& sound = NewParams(all);
            sound.Float(0x6F016DFB, 0.25f).Float(0xABD4A575, 0.75f).Integer(0x595D2C95, 0x81234567)
                .Integer(0xC0547D2F, static_cast<int>(0x89ABCDEF)).Integer(0xB7534DB9, 0x11).Integer(0x2937D81A, -2)
                .Checksum(0xE1F7C4AE, 0x50D70001).Flag(0x31C71B70);
            Params& soundPartial = NewParams(all);
            soundPartial.Integer(0xC0547D2F, 5);
            static CArray controllers;
            static uint32_t controllerElements[8];
            controllers.m_type = Script::ESYMBOLTYPE_STRUCTURE;
            controllers.m_size = 8;
            controllers.m_elements = controllerElements;
            const uint32_t flags[3] = {0x73CEE124, 0x1EEF7085, 0xA483BA67};
            for (int i = 0; i < 8; ++i) {
                Params& c = NewParams(all);
                c.Integer(0x2, i);
                for (int f = 0; f < 3; ++f)
                    if ((i >> f) & 1)
                        c.Flag(flags[f]);
                controllerElements[i] = reinterpret_cast<uint32_t>(c.Struct());
            }
            Params& splitScreen = NewParams(all);
            splitScreen.Integer(0x3, 3);
            Params& taunts = NewParams(all);
            taunts.Integer(0x4, 4);
            Params& options = NewParams(all);
            options.Structure(0xF7720C3F, splitScreen.Struct()).Structure(0xE62B6586, taunts.Struct())
                .Structure(kSound, sound.Struct()).Array(0x37BDB853, &controllers);
            Params& optionsPartial = NewParams(all);
            optionsPartial.Structure(kSound, soundPartial.Struct());
            Params& pros = NewParams(all);
            pros.Integer(0x5, 5);
            Params& career = NewParams(all);
            career.Checksum(0xE3335D2F, 0x1E7E1).Checksum(0x2CC06F5E, 0x30DE);
            Params& careerHalf = NewParams(all);
            careerHalf.Checksum(0x2CC06F5E, 0x30DF);
            Params& global = NewParams(all);
            global.Structure(0x2FCA0578, options.Struct()).Structure(0xC9986BAF, pros.Struct())
                .Structure(0x4DA4937B, career.Struct()).String(0xF36C1878, "custom.cas");
            Params& globalPartial = NewParams(all);
            globalPartial.Structure(0x2FCA0578, optionsPartial.Struct()).Structure(0xC9986BAF, pros.Struct())
                .Structure(0x4DA4937B, careerHalf.Struct());
            Params& globalNoCareer = NewParams(all);
            globalNoCareer.Structure(0x2FCA0578, optionsPartial.Struct()).Structure(0xC9986BAF, pros.Struct());
            for (int platform : {4, 1}) {
                GameVariable<int>(0x007CEFA8) = platform;
                for (Params* g : {&global, &globalPartial, &globalNoCareer}) {
                    for (bool pro : {false, true}) {
                        g_isPro = pro;
                        g_symbolFound = pro;
                        std::memset(g_symbol, 0, sizeof(g_symbol));
                        std::memset(g_script, 0, sizeof(g_script));
                        reinterpret_cast<uint32_t*>(g_script)[0x14 / 4] = ScriptParams();
                        auto* script = reinterpret_cast<Script::CScript*>(g_script);
                        if (original)
                            Original<void (*)(CStruct*, Script::CScript*)>(0x005A7CF0)(g->Struct(), script);
                        else
                            Mc::ReadGlobalInfo(g->Struct(), script);
                        Record({0xEEF0, *reinterpret_cast<uint32_t*>(g_symbol + 0xC)});
                    }
                }
            }

            // ApplyLoadedContents: every type; OptionsAndPros with each section
            Params& otherParams = NewParams(all);
            otherParams.Integer(0x6, 6);
            Params& cat = NewParams(all);
            cat.Structure(Crc::GenerateCRCFromString("other_params"), otherParams.Struct())
                .Array(Crc::GenerateCRCFromString("rotation_info"), reinterpret_cast<void*>(0x0A110000))
                .Array(Crc::GenerateCRCFromString("animation_info"), reinterpret_cast<void*>(0x0A220000))
                .Array(0x9A5FF0A3, reinterpret_cast<void*>(0x0A330000));
            Params& story = NewParams(all);
            story.Integer(0x7, 7);
            Params& storySkater = NewParams(all);
            storySkater.Integer(0x8, 8);
            Params& customSkater = NewParams(all);
            customSkater.Integer(0x9, 9);
            Params& optionsAndPros = NewParams(all);
            optionsAndPros.Structure(0xF55CBD13, global.Struct()).Structure(0x14A9FBC7, story.Struct())
                .Structure(0x0DF2F448, storySkater.Struct()).Structure(0x12BFAC82, customSkater.Struct())
                .Array(0x9A5FF0A3, reinterpret_cast<void*>(0x0A440000));
            Params& callerParams = NewParams(all);
            callerParams.Integer(0xA, 10);
            GameVariable<int>(0x007CEFA8) = 4;
            auto apply = [&](uint32_t type, CStruct* contents) {
                std::memset(g_script, 0, sizeof(g_script));
                reinterpret_cast<uint32_t*>(g_script)[0x14 / 4] = ScriptParams();
                GameVariable<uint8_t>(0x007CECE0) = 0x77;
                CallApply(original ? OriginalFunction(0x005A81B0) : reinterpret_cast<void*>(&Mc::ApplyLoadedContentsStub),
                          type, contents, g_script, callerParams.Struct());
                Record({0xEEF1, GameVariable<uint32_t>(0x007CECDC), GameVariable<uint8_t>(0x007CECE0)});
            };
            for (uint32_t type : types) {
                if (type == Mc::kSaveOptionsAndPros)
                    continue;
                g_noLocalSkater = false;
                apply(type, cat.Struct());
            }
            g_noLocalSkater = true;
            apply(Mc::kSaveCat, cat.Struct());
            for (uint32_t sections : {0u, 1u, 2u, 4u, 8u, 0xFFFFFFFFu}) {
                for (uint32_t index : {0u, 1u}) {
                    g_profileIndex = index;
                    GameVariable<uint32_t>(0x007CECDC) = sections;
                    apply(Mc::kSaveOptionsAndPros, optionsAndPros.Struct());
                }
            }
        }
        // GetSaveInfo (the types whose summary of empty contents is safe)
        for (uint32_t type : {Mc::kSaveCreatedGoals, Mc::kSaveNetworkSettings, Mc::kSaveCreatedGraphic, 0x1234u}) {
            Params p;
            p.Checksum(0x7321A8D6, type);
            Call(original, 0x005A8BF0, Mc::GetSaveInfo, p);
        }
        for (uint32_t& word : g_trace)
            word = Known(word);
        for (uint32_t i = 0; i < g_arenaUsed / 4; ++i)
            g_trace.push_back(ArenaWordOrHigh(g_contentsArenas, kArenaSize, reinterpret_cast<uint32_t*>(g_arena)[i]));
        for (Params* p : all)
            delete p;
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(saved);
    Check(traces[0].size() > 1500, "save contents: the original reached its calls");
    Compare(traces[0], traces[1], "save contents (0x005A7560-0x005A81B0)");
}

} // namespace

SELFTEST(Early, TestSaveContents, RemoveStandIns);
