// Self-tests for PartyMod-patched functions of different subsystems: the game's main (fast exit), the network
// manager's online service task (MOTD fix) and LoadFromMemoryCard (no save checksum test). Each original runs
// with PartyMod's patch bytes written in, against ours; every function they call is stood in for by recording
// code (written over the start of the game's function and of our version of it alike), so the two are compared
// call by call.

#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/file/async.h"
#include "game/file/pre.h"
#include "game/mc/memcard.h"
#include "game/mc/memcard_cfuncs.h"
#include "game/mth/math.h"
#include "game/mth/random.h"
#include "game/net/service.h"
#include "game/script/cache.h"
#include "game/script/struct.h"
#include "game/sio/device.h"
#include "game/sys/main.h"
#include "game/sys/mlp.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

extern "C" void fastExit();

namespace {

using namespace TestStandIns;

// Stand-in script structures: 16 of them, handed out in turn by NewStruct.
alignas(16) uint8_t g_structs[16][16];
int g_nextStruct;
uint32_t StructId(uint32_t s)
{
    auto base = reinterpret_cast<uint32_t>(g_structs);
    return s >= base && s < base + sizeof(g_structs) ? 0x5700 + (s - base) / 16 : s;
}
// NewStruct / CStruct::Init / Clear / DeleteStruct, the game's and ours (the original's inlined pool
// allocation calls Pool::Allocate: stood in too, with the pool global set).
void StandInStructs(StandIns& s, bool poolAllocate)
{
    std::initializer_list<uint32_t> news = {0x00409030, AddressOf(&Script::NewStruct)};
    s.At(news, 0, [](Regs&) {
        Record({0x4E5});
        return g_nextStruct >= 0 ? reinterpret_cast<uint32_t>(g_structs[g_nextStruct++ & 15]) : 0u;
    });
    if (poolAllocate)
        s.At({0x005BA3D0}, 0, [](Regs&) {
            Record({0x4E5});
            return g_nextStruct >= 0 ? reinterpret_cast<uint32_t>(g_structs[g_nextStruct++ & 15]) : 0u;
        });
    s.At({0x00476410, AddressOf(&Script::CStruct::Init)}, 0, [](Regs& r) {
        Record({0x1417, StructId(r.ecx)});
        return r.ecx;
    });
    s.At({0x00477130, AddressOf(&Script::CStruct::Clear)}, 0, [](Regs& r) {
        Record({0xC1EA, StructId(r.ecx)});
        return 0u;
    });
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) {
        Record({0xDE1, StructId(r.args[0])});
        return 0u;
    });
}

// --- the online service task (0x00545880) -------------------------------------------------------------------

alignas(16) uint8_t g_netManager[0x10000];
alignas(16) uint8_t g_memManager[0x600];
uint32_t g_now;
int g_gsiAnswer;
uint32_t g_lobbyItem;
const char g_motd[] = "Message of the day";
const char g_messages[] = "messages";

void TestServiceTask()
{
    std::vector<Saved> saved = {{0x007D1800, 4, {}}, {0x007CE444, 4, {}}};
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInStructs(*s, false);
        auto manager = reinterpret_cast<uint32_t>(g_netManager);
        auto memManager = reinterpret_cast<uint32_t>(g_memManager);
        auto self = [=](uint32_t ecx) { return ecx == manager ? 0x3A3Au : ecx; };
        s->At({0x005453E0}, 4, [=](Regs& r) { Record({0x5E7, self(r.ecx), r.args[0]}); return 0u; });
        s->At({0x004068F0}, 0, [](Regs& r) { Record({0x4E3, r.ecx}); return 0u; });
        s->At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [=](Regs& r) {
            Record({0x9054, r.ecx == memManager, r.args[0]});
            return 0u;
        });
        s->At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [=](Regs& r) {
            Record({0x90B, r.ecx == memManager});
            return 0u;
        });
        s->At({0x005BE970, AddressOf(&Tmr::GetTime)}, 0, [](Regs&) {
            Record({0x713E});
            return g_now;
        });
        s->At({0x00475790}, 0, [](Regs& r) {
            Record({0x5C41, r.args[0], StructId(r.args[1]), r.args[2], r.args[3], r.args[4]});
            return 0u;
        });
        s->At({0x00475420}, 0, [](Regs& r) {
            Record({0x5BA1, r.args[0], StructId(r.args[1])});
            for (int i = 2; i < 10; ++i)
                Record({r.args[i]});
            return 0u;
        });
        s->At({0x004779D0, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, const char*)>(&Script::CStruct::AddString))},
              8, [](Regs& r) {
                  Record({0xADD2, StructId(r.ecx), r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
                  return 0u;
              });
        s->At({0x00478390, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, uint32_t, const char*)>(
                               &Script::CStruct::AddString))},
              12, [](Regs& r) {
                  Record({0xADD3, StructId(r.ecx), r.args[0], r.args[1], Hash(reinterpret_cast<const char*>(r.args[2]))});
                  return 0u;
              });
        s->At({0x00478FD0, AddressOf(static_cast<const char* (*)(uint32_t)>(&Script::GetString))}, 0, [](Regs& r) {
            Record({0x6E75, r.args[0]});
            return reinterpret_cast<uint32_t>(r.args[0] == 0x78D25623 ? g_messages : "no servers");
        });
        s->At({0x00451010}, 0, [](Regs& r) { Record({0x9A05, r.args[0]}); return 0u; });
        s->At({0x005F8690}, 0, [](Regs& r) { Record({0x6510, Hash(reinterpret_cast<const char*>(r.args[0]))}); return 0u; });
        s->At({0x005F8840}, 0, [](Regs&) { Record({0x6511}); return static_cast<uint32_t>(g_gsiAnswer); });
        s->At({0x005FFB10}, 0, [](Regs&) { Record({0x7417}); return 0u; });
        s->At({0x005FFB20}, 0, [](Regs& r) { Record({0x7CA7, r.args[0]}); return 0u; });
        s->At({0x005FF860}, 0, [](Regs&) { Record({0x7C1E}); return 0u; });
        s->At({0x00533240}, 4, [=](Regs& r) { Record({0x71D, self(r.ecx), r.args[0]}); return 0u; });
        s->At({0x005427E0}, 0, [](Regs& r) { Record({0x427E, r.ecx}); return 0u; });
        s->At({0x005428B0}, 0, [](Regs& r) { Record({0x428B, r.ecx}); return 0u; });
        s->At({0x00541A10}, 0, [](Regs& r) { Record({0x41A1, r.ecx}); return 0u; });
        s->At({0x00542970}, 4, [](Regs& r) {
            Record({0x4297, r.ecx, r.args[0]});
            return g_lobbyItem;
        });
        s->At({0x006234FE}, 0, StandInSprintf);
        StandInApi(*s, 0x006450C8, reinterpret_cast<const void*>(&CreateThread), 24, [=](Regs& r) {
            Record({0xC7EA, r.args[0], r.args[1], r.args[2], self(r.args[3]), r.args[4]});
            *reinterpret_cast<DWORD*>(r.args[5]) = 0x4321;
            return 0x1234u;
        });
        StandInApi(*s, 0x006450E0, reinterpret_cast<const void*>(&ResumeThread), 4, [](Regs& r) {
            Record({0x7E5, r.args[0]});
            return 0u;
        });

        GameVariable<uint32_t>(0x007D1800) = memManager;
        std::memset(g_memManager, 0, sizeof(g_memManager));
        *reinterpret_cast<uint32_t*>(g_memManager + 0x140) = 0xBEEF0140;
        alignas(16) uint8_t task[0x20] = {};
        *reinterpret_cast<uint8_t**>(task + 0x1C) = g_netManager;

        struct Case {
            int state;
            uint32_t now, wake, start;
            int gsi;
            int count;
            bool finished;
            uint32_t lobby;
        };
        std::vector<Case> cases;
        for (int state : {-1, 1, 2, 3, 4, 8, 9, 0xB, 0xD, 0xE, 0x10, 0x11, 0x12, 0x13})
            cases.push_back({state, 5000, 300, 1000, 0, 0, false, 0});
        for (uint32_t wake : {0u, 4999u, 5000u, 5001u})
            cases.push_back({0, 5000, wake, 0, 0, 0, false, 0});
        for (int gsi : {0, 1, 2, 3, 4, -1})
            cases.push_back({5, 5000, 7, 1000, gsi, 0, false, 0});
        for (int state : {6, 0xA})
            for (int count : {0, 3, -2})
                cases.push_back({state, 5000, 7, 1000, 0, count, false, 0});
        for (uint32_t now : {1499u, 1500u, 1501u, 15999u, 16000u, 16001u, 999u})
            for (bool finished : {false, true}) {
                cases.push_back({7, now, 7, 1000, 0, 0, finished, 0});
                cases.push_back({0xC, now, 7, 1000, 0, 0, finished, 0});
            }
        for (uint32_t lobby : {0u, 0x10BB1u})
            cases.push_back({0xF, 5000, 7, 1000, 0, 0, false, lobby});

        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            if (original) { // PartyMod's MOTD fix (net.c patchMotdFix)
                RunPatch(0x00545B4F, Dword(500));
                RunPatch(0x00545CDE, Dword(0x588ED87C));
                RunPatch(0x00545CE8, {0x01});
            }
            for (const Case& c : cases) {
                for (size_t i = 0; i < 0x200; ++i)
                    g_netManager[i] = static_cast<uint8_t>(i * 37 + 11);
                std::memset(g_netManager + 0xFDF0, 0xA5, 0x20);
                *reinterpret_cast<uint32_t*>(g_netManager + 0x10) = 0x10B0;
                *reinterpret_cast<uint32_t*>(g_netManager + 0xE0) = 0x7A5C;
                *reinterpret_cast<int*>(g_netManager + 0x1B8) = c.state;
                *reinterpret_cast<int*>(g_netManager + 0x1BC) = 0x66;
                *reinterpret_cast<uint32_t*>(g_netManager + 0x1C0) = c.wake;
                *reinterpret_cast<const char**>(g_netManager + 0xFDF4) = g_motd;
                *reinterpret_cast<uint32_t*>(g_netManager + 0xFE00) = 0x77;
                *reinterpret_cast<uint32_t*>(g_netManager + 0xFE04) = c.start;
                g_netManager[0xFE08] = c.finished;
                g_netManager[0xFE09] = 0x42;
                g_netManager[0xFE0A] = 0x43;
                *reinterpret_cast<uint32_t*>(g_netManager + 0x9950) = 0x99;
                g_now = c.now;
                g_gsiAnswer = c.gsi;
                g_lobbyItem = c.lobby;
                g_nextStruct = 0;
                GameVariable<int>(0x007CE444) = c.count;
                if (original)
                    Original<void(__cdecl*)(const uint8_t*)>(0x00545880)(task);
                else
                    Net::ServiceTask(task);
                Record({0xCA5E});
                for (size_t i = 0; i < 0x200; i += 4)
                    Record({*reinterpret_cast<uint32_t*>(g_netManager + i)});
                for (size_t i = 0xFDF0; i < 0xFE10; i += 4)
                    Record({*reinterpret_cast<uint32_t*>(g_netManager + i)});
                Record({*reinterpret_cast<uint32_t*>(g_netManager + 0x9950)});
            }
            EndRunPatches();
            traces[run] = g_trace;
        }
        delete s;
    }
    RestoreAll(saved);
    Compare(traces[0], traces[1], "service task: every state, the timeouts (PartyMod's 500 ms), GameSpy answers, lobby");
}

// --- LoadFromMemoryCard (0x005A8730) ------------------------------------------------------------------------

alignas(16) uint8_t g_card[0x10];
alignas(16) uint8_t g_file[0x100];
alignas(16) uint8_t g_fileData[0x400];
alignas(16) uint8_t g_fileContents[0x400];
alignas(16) uint8_t g_script[0x20];
alignas(16) uint8_t g_resultParams[0x10];
alignas(16) uint8_t g_params[0x10];
alignas(16) uint8_t g_kept[0x10];
void* g_fileVtable[1];
struct LoadCase {
    int platform;
    bool card, converted, opened;
    bool hasNoApply;
    int noApply;
    bool hasName, hasType;
    uint32_t type;
    int fileSize, expectedSize, readSize;
    int headerSize, version, typeVersion;
    bool newStruct;
};
LoadCase g_load;

void TestLoadFromMemoryCard()
{
    std::vector<Saved> saved = {{0x007CEFA8, 4, {}}, {0x007D1890, 4, {}}, {0x007CECD4, 8, {}},
                                {0x006F3614, 4, {}}, {0x006F361C, 8, {}}};
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInStructs(*s, true);
        using Script::CStruct;
        auto params = reinterpret_cast<uint32_t>(g_params);
        auto id = [=](uint32_t p) {
            return p == params ? 0x9A9Au : p == reinterpret_cast<uint32_t>(g_resultParams) ? 0x9E5u : StructId(p);
        };
        auto data = reinterpret_cast<uint32_t>(g_fileData);
        s->At({0x00450F70}, 0, [](Regs& r) { Record({0x9A05, r.args[0]}); return 0u; });
        s->At({0x00451010}, 0, [](Regs& r) { Record({0x9A06, r.args[0]}); return 0u; });
        s->At({0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
              [=](Regs& r) {
                  Record({0x6E71, id(r.ecx), r.args[0], *reinterpret_cast<uint32_t*>(r.args[1]), r.args[2]});
                  if (g_load.hasNoApply)
                      *reinterpret_cast<int*>(r.args[1]) = g_load.noApply;
                  return static_cast<uint32_t>(g_load.hasNoApply);
              });
        s->At({0x00476C80, AddressOf(static_cast<bool (CStruct::*)(const char*, const char**, int)>(&CStruct::GetString))},
              12, [=](Regs& r) {
                  Record({0x6E75, id(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])),
                          Hash(*reinterpret_cast<const char**>(r.args[1])), r.args[2]});
                  if (g_load.hasName)
                      *reinterpret_cast<const char**>(r.args[1]) = "Skater 1";
                  return static_cast<uint32_t>(g_load.hasName);
              });
        s->At({0x00476A00, AddressOf(static_cast<bool (CStruct::*)(const char*, uint32_t*, int)>(&CStruct::GetChecksum))},
              12, [=](Regs& r) {
                  Record({0x6E7C, id(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])),
                          *reinterpret_cast<uint32_t*>(r.args[1]), r.args[2]});
                  if (g_load.hasType)
                      *reinterpret_cast<uint32_t*>(r.args[1]) = g_load.type;
                  return static_cast<uint32_t>(g_load.hasType);
              });
        s->At({0x005BC730, AddressOf(&Mc::Manager::GetCard)}, 8, [](Regs& r) {
            Record({0xCA4D, r.ecx, r.args[0], r.args[1]});
            return g_load.card ? reinterpret_cast<uint32_t>(g_card) : 0u;
        });
        s->At({0x005B5F10}, 0, [](Regs&) {
            Record({0xF01D});
            return reinterpret_cast<uint32_t>("Folder");
        });
        s->At({0x005A5F70}, 0, [](Regs& r) {
            Record({0x5F70, Hash(reinterpret_cast<const char*>(r.eax)), r.edi});
            strcpy_s(reinterpret_cast<char*>(r.esi), 20, "FILENAME");
            return r.esi;
        });
        s->At({0x005A6390}, 0, [](Regs& r) {
            Record({0x6390, r.edi, Hash(reinterpret_cast<const char*>(r.args[0]))});
            strcpy_s(reinterpret_cast<char*>(r.args[1]), 104, "FolderFile");
            return 0u;
        });
        s->At({0x005A62A0}, 0, [](Regs& r) {
            Record({0x62A0, r.eax, Hash(reinterpret_cast<const char*>(r.ecx))});
            return reinterpret_cast<uint32_t>("pattern");
        });
        s->At({0x005BC740, AddressOf(&Mc::Card::ConvertFilename)}, 4, [](Regs& r) {
            Record({0xC0F, r.ecx == reinterpret_cast<uint32_t>(g_card), Hash(reinterpret_cast<const char*>(r.args[0]))});
            return g_load.converted ? reinterpret_cast<uint32_t>("Save\\converted.sav") : 0u;
        });
        s->At({0x006234FE}, 0, StandInSprintf);
        s->At({0x005BC9B0, AddressOf(&Mc::Card::Open)}, 12, [](Regs& r) {
            Record({0x0BE, r.ecx == reinterpret_cast<uint32_t>(g_card), Hash(reinterpret_cast<const char*>(r.args[0])),
                    r.args[1], r.args[2]});
            return g_load.opened ? reinterpret_cast<uint32_t>(g_file) : 0u;
        });
        s->At({0x005BC800, AddressOf(&Mc::File::Seek)}, 8, [](Regs& r) {
            Record({0x5EE, r.ecx == reinterpret_cast<uint32_t>(g_file), r.args[0], r.args[1]});
            return r.args[1] == 2 ? static_cast<uint32_t>(g_load.fileSize) : 0u;
        });
        s->At({0x005A7340}, 0, [](Regs& r) {
            Record({0x7340, r.ecx});
            return static_cast<uint32_t>(g_load.expectedSize);
        });
        s->At({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 8,
              [=](Regs& r) {
                  Record({0xADDC, id(r.ecx), r.args[0], r.args[1]});
                  return 0u;
              });
        s->At({0x005BBAE0, AddressOf(&Mem::Malloc)}, 0, [](Regs& r) {
            Record({0x3A11, r.args[0]});
            return reinterpret_cast<uint32_t>(g_fileData);
        });
        s->At({0x005BC880, AddressOf(&Mc::File::Read)}, 8, [=](Regs& r) {
            Record({0x8EAD, r.ecx == reinterpret_cast<uint32_t>(g_file), r.args[0] == data, r.args[1]});
            std::memcpy(reinterpret_cast<void*>(r.args[0]), g_fileContents, sizeof(g_fileContents));
            return static_cast<uint32_t>(g_load.readSize);
        });
        // (the original's checksum: computed, but PartyMod's NOP ignores it - not recorded)
        s->At({0x00401AA0}, 0, [](Regs&) { return 0x12345678u; });
        s->At({0x005A6210}, 0, [](Regs& r) {
            Record({0x6210, r.eax});
            return static_cast<uint32_t>(g_load.typeVersion);
        });
        s->At({0x0047A0C0}, 0, [=](Regs& r) {
            uint32_t offset = r.args[1] - data;
            Record({0x47A0, id(r.args[0]), offset, HashBytes(reinterpret_cast<uint8_t*>(r.args[1]), 0x100 - offset)});
            return r.args[1] + 0x30;
        });
        s->At({0x005A81B0}, 0, [=](Regs& r) {
            Record({0x81B0, r.eax, id(r.esi), r.args[0], id(r.args[1])});
            return 0u;
        });
        s->At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [=](Regs& r) {
            Record({0xA99E, id(r.ecx), id(r.args[0])});
            return 0u;
        });
        s->At({0x00475790}, 0, [=](Regs& r) {
            Record({0x5C41, r.args[0], id(r.args[1]), r.args[2], r.args[3], r.args[4]});
            return 0u;
        });
        s->At({0x005BC8B0, AddressOf(&Mc::File::Close)}, 0, [](Regs& r) {
            Record({0xC105, r.ecx == reinterpret_cast<uint32_t>(g_file)});
            return 1u;
        });
        g_fileVtable[0] = s->Thunk(4, [](Regs& r) {
            Record({0xDE7, r.ecx == reinterpret_cast<uint32_t>(g_file), r.args[0]});
            return 0u;
        });
        s->At({0x005BBB60, AddressOf(&Mem::FreeMalloc)}, 0, [=](Regs& r) {
            Record({0xF3EE, r.args[0] == data});
            return 0u;
        });

        *reinterpret_cast<void***>(g_file) = g_fileVtable;
        *reinterpret_cast<uint8_t**>(g_script + 0x14) = g_resultParams;
        GameVariable<uint32_t>(0x007D1890) = 0x3C3C0000;
        GameVariable<int>(0x006F3614) = 0;
        GameVariable<uint32_t>(0x006F361C) = 0x9001;

        std::vector<LoadCase> cases;
        LoadCase base = {4, true, true, true, false, 0, true, true, 0x3BF882CC, 0x100, 0x100, 0x100, 0x100, 7, 7, true};
        for (int platform : {0, 1, 2, 3, 4, 5, 6, 7, 8, -1}) {
            LoadCase c = base;
            c.platform = platform;
            cases.push_back(c);
        }
        auto vary = [&](auto change) {
            LoadCase c = base;
            change(c);
            cases.push_back(c);
        };
        vary([](LoadCase& c) { c.card = false; });
        vary([](LoadCase& c) { c.card = false; c.platform = 2; });
        vary([](LoadCase& c) { c.converted = false; });
        vary([](LoadCase& c) { c.converted = false; c.platform = 7; });
        vary([](LoadCase& c) { c.opened = false; });
        vary([](LoadCase& c) { c.hasName = false; c.hasType = false; });
        vary([](LoadCase& c) { c.expectedSize = 0xFF; });
        vary([](LoadCase& c) { c.readSize = 0xFF; });
        vary([](LoadCase& c) { c.headerSize = 500000; });
        vary([](LoadCase& c) { c.headerSize = 500001; });
        vary([](LoadCase& c) { c.headerSize = -5; });
        vary([](LoadCase& c) { c.typeVersion = 8; });
        vary([](LoadCase& c) { c.type = 0xCA41692D; });
        vary([](LoadCase& c) { c.type = 0xCA41692D; c.headerSize = 0x44; });
        vary([](LoadCase& c) { c.type = 0xCA41692D; c.headerSize = 0x30; });
        vary([](LoadCase& c) { c.hasNoApply = true; c.noApply = 0; });
        vary([](LoadCase& c) { c.hasNoApply = true; c.noApply = 2; });
        vary([](LoadCase& c) { c.hasNoApply = true; c.noApply = 1; c.type = 0x61A1BC57; });
        vary([](LoadCase& c) { c.hasNoApply = true; c.noApply = -1; c.type = 0x61A1BC57; c.platform = 3; });
        vary([](LoadCase& c) { c.newStruct = false; c.expectedSize = 1; });

        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            if (original)
                RunPatch(0x005A8A01, {0x90, 0x90}); // PartyMod (net.c patchMotdFix)
            for (const LoadCase& c : cases) {
                g_load = c;
                g_nextStruct = c.newStruct ? 0 : -1;
                GameVariable<int>(0x007CEFA8) = c.platform;
                GameVariable<uint32_t>(0x007CECD4) = 0xD4D4;
                GameVariable<uint32_t>(0x007CECD8) = 0xD8D8;
                std::memset(g_fileData, 0xEE, sizeof(g_fileData));
                for (size_t i = 0; i < sizeof(g_fileContents); ++i)
                    g_fileContents[i] = static_cast<uint8_t>(i * 7 + 3);
                uint32_t header[5] = {0x11111111, 0x22, 0x33, static_cast<uint32_t>(c.headerSize),
                                      static_cast<uint32_t>(c.version)};
                std::memcpy(g_fileContents, header, sizeof(header));
                bool result;
                auto* p = reinterpret_cast<CStruct*>(g_params);
                auto* script = reinterpret_cast<Script::CScript*>(g_script);
                if (original)
                    result = Original<bool(__cdecl*)(CStruct*, Script::CScript*)>(0x005A8730)(p, script);
                else
                    result = Mc::LoadFromMemoryCard(p, script);
                Record({0xCA5E, result, GameVariable<uint32_t>(0x007CECD4) == 0xD4D4 ? 0xD4D4u : StructId(GameVariable<uint32_t>(0x007CECD4)),
                        GameVariable<uint32_t>(0x007CECD8), HashBytes(g_fileData, sizeof(g_fileData))});
            }
            EndRunPatches();
            traces[run] = g_trace;
        }
        delete s;
    }
    RestoreAll(saved);
    Compare(traces[0], traces[1],
            "LoadFromMemoryCard: platforms, no card/name/file, bad size/version, obfuscated type, kept or applied");
}

// --- the game's main (0x004E1EF0) ---------------------------------------------------------------------------

alignas(16) uint8_t g_singletons[40][0x40];
uint32_t SingletonId(uint32_t p)
{
    auto base = reinterpret_cast<uint32_t>(g_singletons);
    return p >= base && p < base + sizeof(g_singletons) ? 0x5100 + (p - base) : p;
}
alignas(16) uint8_t g_heap[0x10];
void* g_heapVtable[2];
int g_loops;

void TestMain()
{
    std::vector<Saved> saved = {{0x007D1800, 4, {}}, {0x007CEFA0, 4, {}}, {0x007CC978, 1, {}}, {0x007D6A2C, 1, {}},
                                {0x006FA7D0, 4, {}}, {0x007CCE14, 4, {}}, {0x00696D40, 4, {}}};
    SaveAll(saved);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        auto memManager = reinterpret_cast<uint32_t>(g_memManager);
        auto record = [](uint32_t tag) {
            return [tag](Regs& r) {
                Record({tag, SingletonId(r.ecx)});
                return 0u;
            };
        };
        auto recordArg = [](uint32_t tag) {
            return [tag](Regs& r) {
                Record({tag, SingletonId(r.ecx), SingletonId(r.args[0])});
                return 0u;
            };
        };
        // (cdecl functions: ECX is whatever the caller left - not recorded)
        auto plain = [](uint32_t tag) {
            return [tag](Regs&) {
                Record({tag});
                return 0u;
            };
        };
        s->At({0x005B60B0}, 0, [](Regs& r) { Record({0xC0F6, r.args[0], r.args[1]}); return 0u; });
        s->At({0x005BBE80, AddressOf(&Mem::Manager::InitStandardHeaps)}, 0, [=](Regs& r) {
            Record({0x5EA9, r.ecx == memManager});
            return 0u;
        });
        s->At({0x004E0DA0}, 0, plain(0xDA0));
        s->At({0x004E0FE0}, 0, plain(0xFE0));
        s->At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
            Record({0xDB6, Hash(reinterpret_cast<const char*>(r.args[0]))});
            const char* format = reinterpret_cast<const char*>(r.args[0]);
            if (std::strchr(format, '%'))
                Record({r.args[1]});
            if (std::strstr(format, "%s"))
                Record({Hash(reinterpret_cast<const char*>(r.args[2]))});
            return 0xFFFFFFFFu;
        });
        StandInApi(*s, 0x006450DC, reinterpret_cast<const void*>(&SetThreadExecutionState), 4, [](Regs& r) {
            Record({0x5E7E, r.args[0]});
            return 0u;
        });
        s->At({0x00403DA0, AddressOf(&Mth::InitialRand)}, 0, [](Regs& r) { Record({0x4A4D, r.args[0]}); return 0u; });
        s->At({0x00403F70, AddressOf(&Mth::InitSinLookupTable)}, 0, plain(0x5191));
        g_heapVtable[0] = s->Thunk(0, [](Regs& r) {
            Record({0x9054, r.ecx == reinterpret_cast<uint32_t>(g_heap)});
            return 0u;
        });
        g_heapVtable[1] = s->Thunk(0, [](Regs& r) {
            Record({0x90B, r.ecx == reinterpret_cast<uint32_t>(g_heap)});
            if (--g_loops <= 0)
                GameVariable<bool>(0x007D6A2C) = true;
            return 0u;
        });
        s->At({0x00478C90, AddressOf(&Script::CreateSymbolTable)}, 0, plain(0x5E7A));
        s->At({0x005BC3C0, AddressOf(&Mem::PoolManager::SetUp)}, 0, [](Regs& r) {
            Record({0x9001, r.args[0], r.args[1]});
            return 0u;
        });
        s->At({0x005B8E90, AddressOf(&Pre::CreateManager)}, 0, [](Regs& r) {
            Record({0x93E, r.args[0] & 0xFF});
            return reinterpret_cast<uint32_t>(g_singletons[39]);
        });
        s->At({0x005BEA50, AddressOf(&Tmr::Init)}, 0, plain(0x713));
        s->At({0x005B9DF0, AddressOf(&File::AsyncInit)}, 0, plain(0xA51));
        struct Create {
            uint32_t address;
            uint32_t ours;
        };
        const Create creates[] = {
            {0x00463730, 0}, {0x0044F260, AddressOf(&Mlp::CreateManager)}, {0x0044F6F0, 0},
            {0x005BE670, AddressOf(&Sio::CreateManager)},
            {0x005BC900, AddressOf(&Mc::CreateManager)}, {0x0044E7F0, 0},
            {0x005B6CA0, AddressOf(&File::CreateAsyncLoaderModule)},
            {0x00476040, AddressOf(&Script::CreateScriptCache)}, {0x004088E0, 0},
            {0x0047BA60, 0}, {0x00455880, 0}, {0x0045F7A0, 0}, {0x00589180, 0}, {0x00553DB0, 0}, {0x0057C0D0, 0},
            {0x0057EED0, 0}, {0x005691A0, 0}, {0x0054FF80, 0}, {0x0053F520, 0}, {0x00590160, 0}, {0x004ACAF0, 0},
            {0x0045D690, 0}, {0x00574540, 0}};
        int index = 0;
        for (const Create& c : creates) {
            Handler handler = [index](Regs& r) {
                Record({0xC0000 + static_cast<uint32_t>(index), r.args[0] & 0xFF});
                return reinterpret_cast<uint32_t>(g_singletons[index]);
            };
            if (c.ours)
                s->At({c.address, c.ours}, 0, handler);
            else
                s->At({c.address}, 0, handler);
            ++index;
        }
        const Create releases[] = {
            {0x00574910, 0}, {0x0045D6E0, 0}, {0x004AACF0, 0}, {0x0058D290, 0}, {0x005395C0, 0}, {0x0054EBE0, 0},
            {0x00564C10, 0}, {0x0057FDB0, 0}, {0x0057C120, 0}, {0x00552F70, 0}, {0x005880E0, 0}, {0x0045F1E0, 0},
            {0x004558D0, 0}, {0x0047C6C0, 0}, {0x00407980, 0}, {0x00475A00, AddressOf(&Script::ReleaseScriptCache)},
            {0x005B6620, AddressOf(&File::DestroyAsyncLoaderModule)}, {0x0044E6B0, 0},
            {0x005BC950, AddressOf(&Mc::ReleaseManager)}, {0x005BDCF0, AddressOf(&Sio::ReleaseManager)},
            {0x0044F740, 0}, {0x0044F2B0, AddressOf(&Mlp::ReleaseManager)}, {0x00461FA0, 0},
            {0x005B9510, AddressOf(&Pre::DestroyManager)}};
        index = 0;
        for (const Create& c : releases) {
            Handler handler = plain(0xE0000 + static_cast<uint32_t>(index));
            if (c.ours)
                s->At({c.address, c.ours}, 0, handler);
            else
                s->At({c.address}, 0, handler);
            ++index;
        }
        s->At({0x0044E610}, 4, [](Regs& r) { Record({0xE610, SingletonId(r.ecx), r.args[0] & 0xFF}); return 0u; });
        s->At({0x005A5E70}, 0, plain(0x5E70));
        s->At({0x005A5B20}, 0, plain(0x5B20));
        s->At({0x004694E0}, 0, plain(0x94E0));
        s->At({0x004E4860}, 0, plain(0x4860));
        s->At({0x00406C90}, 4, recordArg(0x6C90));
        s->At({0x00406CB0}, 0, record(0x6CB0));
        s->At({0x0044F3C0}, 4, recordArg(0xF3C0));
        s->At({0x0044F310}, 4, recordArg(0xF310));
        s->At({0x0044F3E0}, 4, recordArg(0xF3E0));
        s->At({0x0044F530}, 0, record(0xF530));
        s->At({0x0048C1D0}, 0, [](Regs& r) { Record({0xC1D0, r.args[0]}); return 0u; });
        s->At({0x0044EEF0, AddressOf(&Mlp::Manager::MainLoop)}, 0, record(0xEEF0));
        void* exitThunk = s->At({Resolve(reinterpret_cast<const void*>(&fastExit))}, 0, plain(0xFA57));

        std::memset(g_memManager, 0, sizeof(g_memManager));
        *reinterpret_cast<uint8_t**>(g_memManager + 0x114) = g_heap;
        *reinterpret_cast<void***>(g_heap) = g_heapVtable;
        GameVariable<uint32_t>(0x007D1800) = memManager;
        GameVariable<uint32_t>(0x007CCE14) = 0xCE14;
        GameVariable<void*>(0x00696D40) = g_singletons[38];
        for (int i = 0; i < 40; ++i)
            for (int j = 0; j < 0x40; j += 4)
                *reinterpret_cast<uint32_t*>(&g_singletons[i][j]) = 0x70000000u + i * 0x100 + j;

        static char arg0[] = "THUG2.exe", demo[] = "demo", demox[] = "demox", dem[] = "dem", other[] = "-x";
        struct Case {
            int argc;
            char* argv[4];
            int language;
            int loops; // 0: quit already asked for
        };
        const Case cases[] = {
            {1, {arg0}, 0, 1},          {2, {arg0, demo}, 3, 2},       {2, {arg0, demox}, 5, 1},
            {2, {arg0, dem}, 1, 0},     {3, {arg0, demo, other}, 5, 1}, {2, {arg0, other}, 4, 1},
            {0, {nullptr}, 2, 1},
        };
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            // PartyMod: fastExit called in place of the profile pop after the main loop (main.c patchFastExit)
            if (original)
                RunPatch(0x004E2492, Branch(0xE8, 0x004E2492, exitThunk));
            for (const Case& c : cases) {
                GameVariable<int>(0x007CEFA0) = c.language;
                GameVariable<bool>(0x007CC978) = false;
                GameVariable<bool>(0x007D6A2C) = c.loops == 0;
                GameVariable<uint32_t>(0x006FA7D0) = 0x7D0;
                g_loops = c.loops;
                char* argv[4] = {c.argv[0], c.argv[1], c.argv[2], c.argv[3]};
                int result = original ? Original<int(__cdecl*)(int, char**)>(0x004E1EF0)(c.argc, argv)
                                      : Sys::Main(c.argc, argv);
                Record({0xCA5E, static_cast<uint32_t>(result), GameVariable<uint8_t>(0x007CC978),
                        GameVariable<uint32_t>(0x006FA7D0)});
            }
            EndRunPatches();
            traces[run] = g_trace;
        }
        delete s;
    }
    RestoreAll(saved);
    Compare(traces[0], traces[1], "main: arguments, demo flag, languages, 0-2 loops, singletons, modules, fast exit");
}

} // namespace

SELFTEST(Early, TestServiceTask, RemoveStandIns);
SELFTEST(Early, TestLoadFromMemoryCard, RemoveStandIns);
SELFTEST(Early, TestMain, RemoveStandIns);
