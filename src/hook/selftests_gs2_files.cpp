// Self-tests for the save file commands (save_files_cfuncs.cpp): each original against ours on the same params, the
// card, the files and the pools stood in for by recording code - no file is touched.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/mc/memcard.h"
#include "game/mc/save_files_cfuncs.h"
#include "game/mc/save_types.h"
#include "game/pcm/music.h"
#include "game/pcm/stream_commands.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "hook/cfunc_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using namespace CFuncTests;
using Script::CStruct;

alignas(16) uint8_t g_card[0x10];
alignas(16) uint8_t g_file[0x100];
void* g_fileVtable[4];
alignas(16) uint8_t g_manager[0x600];
bool g_noCard;
bool g_noFile;
bool g_noConvert;
uint32_t g_failingDelete; // the hash of a name whose delete fails
bool g_removeResult;
uint32_t g_fileSize;
uint32_t g_poolNumber;
alignas(16) uint32_t g_poolObjects[8][0x18]; // stand-in pools: their first field the block

uint32_t Pointer(uint32_t value)
{
    if (value == reinterpret_cast<uint32_t>(g_card))
        return 0xCA0D;
    if (value == reinterpret_cast<uint32_t>(g_file))
        return 0xF11E;
    if (value == reinterpret_cast<uint32_t>(g_manager))
        return 0x3A3A;
    if (value == ScriptParams())
        return 0x5001;
    return value;
}

void StandInCalls(StandIns& s)
{
    s.At({0x005BC730, AddressOf(&Mc::Manager::GetCard)}, 8, [](Regs& r) {
        Record({0xC730, r.ecx, r.args[0], r.args[1]});
        return g_noCard ? 0u : reinterpret_cast<uint32_t>(g_card);
    });
    s.At({0x00450F70, AddressOf(&Pcm::PauseMusic)}, 0, [](Regs& r) { Record({0x0F70, r.args[0]}); return 0u; });
    s.At({0x00451010, AddressOf(&Pcm::PauseStream)}, 0, [](Regs& r) { Record({0x1010, r.args[0]}); return 0u; });
    s.At({0x005BC7F0, AddressOf(&Mc::Card::AlwaysTrue)}, 4, [](Regs& r) {
        uint32_t hash = Hash(reinterpret_cast<const char*>(r.args[0]));
        Record({0xC7F0, Pointer(r.ecx), hash});
        return hash == g_failingDelete ? 0x100u : 0x101u;
    });
    s.At({0x005BC770, AddressOf(&Mc::Card::RemoveFile)}, 4, [](Regs& r) {
        Record({0xC770, Pointer(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0]))});
        return g_removeResult ? 0x201u : 0x200u;
    });
    s.At({0x005BC740, AddressOf(&Mc::Card::ConvertFilename)}, 4, [](Regs& r) {
        Record({0xC740, Pointer(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0]))});
        return g_noConvert ? 0u : reinterpret_cast<uint32_t>("converted\\name.prk");
    });
    s.At({0x005BC9B0, AddressOf(&Mc::Card::Open)}, 12, [](Regs& r) {
        Record({0xC9B0, Pointer(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], r.args[2]});
        return g_noFile ? 0u : reinterpret_cast<uint32_t>(g_file);
    });
    s.At({0x005BC800, AddressOf(&Mc::File::Seek)}, 8, [](Regs& r) {
        Record({0xC800, Pointer(r.ecx), r.args[0], r.args[1]});
        return g_fileSize;
    });
    s.At({0x005BC840, AddressOf(&Mc::File::Flush)}, 0, [](Regs& r) { Record({0xC840, Pointer(r.ecx)}); return 1u; });
    s.At({0x005BC8B0, AddressOf(&Mc::File::Close)}, 0, [](Regs& r) { Record({0xC8B0, Pointer(r.ecx)}); return 1u; });
    g_fileVtable[0] = s.Thunk(4, [](Regs& r) { Record({0xDE1E, Pointer(r.ecx), r.args[0]}); return r.ecx; });
    s.At({0x005B5F10}, 0, [](Regs&) { Record({0x5F10}); return reinterpret_cast<uint32_t>("BASLUS-21012"); });
    s.At({0x006234FE}, 0, StandInSprintf);
    using GetIntegerFn = int (*)(uint32_t);
    s.At({0x00478F60, AddressOf(static_cast<GetIntegerFn>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x8F60, r.args[0]});
        return (r.args[0] >> 16) + 0x3000;
    });
    using AddIntegerFn = void (CStruct::*)(uint32_t, int);
    s.At({0x00477B80, AddressOf(static_cast<AddIntegerFn>(&CStruct::AddInteger))}, 8, [](Regs& r) {
        Record({0x7B80, Pointer(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    using AddIntegerNamedFn = void (CStruct::*)(const char*, int);
    s.At({0x00477C30, AddressOf(static_cast<AddIntegerNamedFn>(&CStruct::AddInteger))}, 8, [](Regs& r) {
        Record({0x7C30, Pointer(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
    s.At({0x00477130, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) { Record({0x7130, r.ecx}); return 0u; });
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) { Record({0x9070, r.args[0]}); return 0u; });
    // the pools
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Record({0xB0E0, Pointer(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs& r) { Record({0xB110, Pointer(r.ecx)}); return 0u; });
    // a pool "made": as Script::CreatePool leaves the pair
    auto made = [](uint32_t pairAddress, uint32_t itemSize, uint32_t count, const char* name) {
        auto& pair = GameVariable<Mem::PoolPair>(pairAddress);
        Record({0xC4EA, pairAddress, itemSize, count, Hash(name), static_cast<uint32_t>(pair.current)});
        uint32_t* pool = g_poolObjects[++g_poolNumber % 8];
        pool[0] = 0x9000 + g_poolNumber; // (its block)
        pair.pools[pair.current & 1] = reinterpret_cast<Mem::Pool*>(pool);
        reinterpret_cast<uint8_t*>(&pair.unknown4)[pair.current & 1] = 1;
    };
    s.At({0x0046F110}, 0, [made](Regs& r) { made(0x006F2338, 0x10, r.args[0], reinterpret_cast<const char*>(r.args[1])); return 0u; });
    s.At({0x0046F190}, 0, [made](Regs& r) { made(0x006F3614, 0x08, r.args[0], reinterpret_cast<const char*>(r.args[1])); return 0u; });
    s.At({0x0046F210}, 0, [made](Regs& r) { made(0x006F3654, 0x10, r.args[0], reinterpret_cast<const char*>(r.args[1])); return 0u; });
    s.At({AddressOf(&Script::CreatePool)}, 0, [made](Regs& r) {
        made(r.args[0], r.args[1], r.args[2], reinterpret_cast<const char*>(r.args[3]));
        return 0u;
    });
    s.At({0x00401000, AddressOf(&Mem::Free)}, 0, [](Regs& r) { Record({0x1000, r.args[0]}); return 0u; });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) { Record({0xABA0, Pointer(r.ecx), r.args[0]}); return 0u; });
}

void TestSaveFileCFuncs()
{
    std::vector<Saved> saved = {{0x007CEFA8, 4, {}}, {0x007D1890, 4, {}}, {0x007D1800, 4, {}}, {0x007CECD4, 8, {}},
                                {0x006F2338, 16, {}}, {0x006F3614, 16, {}}, {0x006F3654, 16, {}}, {0x006F3624, 4, {}}};
    SaveAll(saved);
    UseEmptySymbolTable();
    auto* standIns = new StandIns;
    StandInCalls(*standIns);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        g_poolNumber = 0;
        std::memset(g_file, 0, sizeof(g_file));
        *reinterpret_cast<void***>(g_file) = g_fileVtable;
        std::memset(g_manager, 0, sizeof(g_manager));
        reinterpret_cast<uint32_t*>(g_manager)[0x110 / 4] = 0x70D00000;
        GameVariable<void*>(0x007D1800) = g_manager;
        GameVariable<uint32_t>(0x007D1890) = 0x3C000000;
        auto call = [&](uint32_t address, CFunc ours, Params& params) { Call(original, address, ours, params); };

        // DeleteMemCardFile: each platform; with or without the card file name; failures at each step
        const uint32_t types[] = {Mc::kSavePark, Mc::kSaveCat, Mc::kSaveCreatedGoals, Mc::kSaveCreatedGraphic,
                                  Mc::kSaveNetworkSettings, Mc::kSaveOptionsAndPros, 0x99u};
        for (int platform = 0; platform <= 8; ++platform) {
            GameVariable<int>(0x007CEFA8) = platform;
            for (int variant = 0; variant < 5; ++variant) {
                g_noCard = variant == 3;
                g_removeResult = variant != 2;
                g_failingDelete = variant == 1   ? Hash("/BASLUS-21012abcdefgf/icon.sys")
                                  : variant == 4 ? Hash("/BASLUS-21012abcdefgf/BASLUS-21012abcdefgf")
                                                 : 0;
                Params withCard;
                withCard.String(Crc::GenerateCRCFromString("CardFileName"), "/BASLUS-21012abcdefgf/BASLUS-21012abcdefgf")
                    .Checksum(Crc::GenerateCRCFromString("Type"), Mc::kSaveCat);
                call(0x005A6EA0, Mc::DeleteMemCardFile, withCard);
                for (uint32_t type : types) {
                    Params named;
                    named.String(Crc::GenerateCRCFromString("UserFileName"), "my save")
                        .Checksum(Crc::GenerateCRCFromString("Type"), type)
                        .String(Crc::GenerateCRCFromString("XBoxDirectoryName"), "x");
                    call(0x005A6EA0, Mc::DeleteMemCardFile, named);
                }
                Params noSlash;
                noSlash.String(Crc::GenerateCRCFromString("CardFileName"), "nofolderh");
                call(0x005A6EA0, Mc::DeleteMemCardFile, noSlash);
            }
        }
        g_noCard = false;
        // GetMemCardSpaceRequired
        for (int platform : {1, 4, 5}) {
            GameVariable<int>(0x007CEFA8) = platform;
            for (uint32_t type : types) {
                Params p;
                p.Checksum(0, type);
                call(0x005A8500, Mc::GetMemCardSpaceRequired, p);
            }
        }
        // MemCardFileExists: each platform; no card / name / type; the name not converted; no file
        for (int platform = 0; platform <= 8; ++platform) {
            GameVariable<int>(0x007CEFA8) = platform;
            for (int variant = 0; variant < 6; ++variant) {
                g_noCard = variant == 1;
                g_noConvert = variant == 2;
                g_noFile = variant == 3;
                g_fileSize = 0x8123 + static_cast<uint32_t>(variant);
                Params p;
                if (variant != 4)
                    p.String(Crc::GenerateCRCFromString("Name"), "park one");
                if (variant != 5)
                    p.Checksum(Crc::GenerateCRCFromString("Type"), Mc::kSavePark);
                call(0x005A8550, Mc::MemCardFileExists, p);
            }
        }
        g_noCard = g_noConvert = g_noFile = false;
        // ClearMemCardDataForUpload: kept or not
        for (uint32_t kept : {0x0CEC0000u, 0u}) {
            GameVariable<uint32_t>(0x007CECD4) = kept;
            GameVariable<uint32_t>(0x007CECD8) = 0x1234;
            Params none;
            call(0x005A8BB0, Mc::ClearMemCardDataForUpload, none);
            Record({GameVariable<uint32_t>(0x007CECD4), GameVariable<uint32_t>(0x007CECD8)});
        }
        // the temporary pools: made (from current pool 0 and 1), already there, removed (owned or not)
        for (int start : {0, 1}) {
            for (uint32_t pools : {0x006F2338u, 0x006F3614u, 0x006F3654u}) {
                std::memset(reinterpret_cast<void*>(pools), 0, 16);
                GameVariable<int>(pools) = 0;
            }
            if (start == 1)
                GameVariable<uint32_t>(0x006F2338 + 12) = reinterpret_cast<uint32_t>(g_poolObjects[7]); // the reserve there
            Params none;
            call(0x005A8DA0, Mc::CreateTemporaryMemCardPools, none);
            for (uint32_t pools : {0x006F2338u, 0x006F3614u, 0x006F3654u})
                for (int i = 0; i < 4; ++i)
                    Record({GameVariable<uint32_t>(pools + 4 * i)});
            GameVariable<uint8_t>(0x006F3614 + 5) = 0; // the structure pool not owned
            call(0x005A9700, Mc::RemoveTemporaryMemCardPools, none);
            for (uint32_t pools : {0x006F2338u, 0x006F3614u, 0x006F3654u})
                for (int i = 0; i < 4; ++i)
                    Record({GameVariable<uint32_t>(pools + 4 * i)});
        }
        for (uint32_t& word : g_trace)
            word = Pointer(word);
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(saved);
    Check(traces[0].size() > 3000, "save file commands: the original reached its calls");
    Compare(traces[0], traces[1], "save file commands (0x005A6EA0-0x005A9700)");
}

} // namespace

SELFTEST(Early, TestSaveFileCFuncs, RemoveStandIns);
