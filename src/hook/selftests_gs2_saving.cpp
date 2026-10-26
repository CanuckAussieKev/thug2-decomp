// Self-tests for GetMemCardDirectoryListing, SaveToMemoryCard and the writer's callback (save_files_cfuncs.cpp):
// each original against ours, the card, its file list, the files, the pools and the save helpers stood in for by
// recording code - no file is touched; the bytes written are recorded.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string.h>
#include <vector>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/mc/memcard.h"
#include "game/mc/save_contents.h"
#include "game/mc/save_files_cfuncs.h"
#include "game/mc/save_types.h"
#include "game/pcm/music.h"
#include "game/pcm/stream_commands.h"
#include "game/script/struct.h"
#include "game/script/struct_buffer.h"
#include "game/sfx/sfx.h"
#include "hook/cfunc_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using namespace CFuncTests;
using Script::CStruct;

constexpr unsigned int kArenaSize = 0x10000;
ArenaPair<kArenaSize> g_savingStorage;
uint8_t (*const g_savingArenas)[kArenaSize] = g_savingStorage.Get();
uint8_t* g_arena;
uint32_t g_arenaUsed;

alignas(16) uint8_t g_card[0x10];
alignas(16) uint8_t g_openFile[0x100];
void* g_nodeVtable[4];
void* g_fileVtable[4];
bool g_noCard;
bool g_noFile;
int g_cardError;
int g_writeShort; // 1: the header write short, 2: the padding write short
bool g_flushFails;
uint32_t g_fileSizeValue;
uint32_t g_mainSize;
uint32_t g_summarySize;

// The card's files: names, dates, and the first 0x78 bytes each holds.
struct ListedFile {
    Mc::File file;
    uint8_t header[0x78];
};
ListedFile g_files[80];
int g_fileCount;
int g_openIndex;

uint32_t Relocate(uint32_t word) { return ArenaWord(g_savingArenas, kArenaSize, word); }
uint32_t Bump(uint32_t size)
{
    uint32_t at = (g_arenaUsed + 15) & ~15u;
    g_arenaUsed = at + size;
    std::memset(g_arena + at, 0xCD, size);
    return reinterpret_cast<uint32_t>(g_arena + at);
}
uint32_t Mark(uint32_t value)
{
    if (value == reinterpret_cast<uint32_t>(g_card))
        return 0xCA0D;
    if (value == reinterpret_cast<uint32_t>(g_openFile))
        return 0xF11E;
    if (value == ScriptParams())
        return 0x5001;
    for (int i = 0; i < 80; ++i)
        if (value == reinterpret_cast<uint32_t>(&g_files[i].file))
            return 0xF100 + static_cast<uint32_t>(i);
    return Relocate(value);
}

// The entries' years (set through the stand-in AddInteger), for the stand-in IsLater.
std::vector<std::pair<uint32_t, int>> g_years;
int YearOf(uint32_t entry)
{
    for (auto& y : g_years)
        if (y.first == entry)
            return y.second;
    return 0;
}

void StandInCalls(StandIns& s)
{
    s.At({0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, [](Regs& r) {
        Record({0xA3D0, r.ecx});
        return Bump(0x10);
    });
    s.At({0x005BBAE0, AddressOf(&Mem::Malloc)}, 0, [](Regs& r) {
        Record({0xBAE0, r.args[0]});
        return Bump(r.args[0] < 0x1000 ? r.args[0] : 0x10);
    });
    s.At({0x005BBB60, AddressOf(&Mem::FreeMalloc)}, 0, [](Regs& r) { Record({0xBB60, Mark(r.args[0])}); return 0u; });
    s.At({0x00477130}, 0, [](Regs& r) { Record({0x7090, Mark(r.ecx)}); return 0u; });
    s.At({0x00477090, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) { Record({0x7090, Mark(r.ecx)}); return 0u; });
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) { Record({0x9070, Mark(r.args[0])}); return 0u; });
    using RemoveFn = void (CStruct::*)(uint32_t);
    s.At({0x004776B0, AddressOf(static_cast<RemoveFn>(&CStruct::RemoveComponent))}, 4, [](Regs& r) {
        Record({0x76B0, Mark(r.ecx), r.args[0]});
        return 0u;
    });
    using AddChecksumFn = void (CStruct::*)(uint32_t, uint32_t);
    s.At({0x00477D40, AddressOf(static_cast<AddChecksumFn>(&CStruct::AddChecksum))}, 8, [](Regs& r) {
        Record({0x7D40, Mark(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    using AddIntegerFn = void (CStruct::*)(uint32_t, int);
    s.At({0x00477B80, AddressOf(static_cast<AddIntegerFn>(&CStruct::AddInteger))}, 8, [](Regs& r) {
        Record({0x7B80, Mark(r.ecx), r.args[0], r.args[1]});
        if (r.args[0] == 0x447D8CC8) // year
            g_years.push_back({r.ecx, static_cast<int>(r.args[1])});
        return 0u;
    });
    using AddStringFn = void (CStruct::*)(uint32_t, const char*);
    s.At({0x004779D0, AddressOf(static_cast<AddStringFn>(&CStruct::AddString))}, 8, [](Regs& r) {
        Record({0x79D0, Mark(r.ecx), r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
        return 0u;
    });
    using AddStringNamedFn = void (CStruct::*)(const char*, const char*);
    s.At({0x00477A90, AddressOf(static_cast<AddStringNamedFn>(&CStruct::AddString))}, 8, [](Regs& r) {
        Record({0x7A90, Mark(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])), Hash(reinterpret_cast<const char*>(r.args[1]))});
        return 0u;
    });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Record({0x8540, Mark(r.ecx), Mark(r.args[0])});
        return 0u;
    });
    using AddArrayPointerFn = void (CStruct::*)(uint32_t, Script::CArray*);
    s.At({0x004780D0, AddressOf(static_cast<AddArrayPointerFn>(&CStruct::AddArrayPointer))}, 8, [](Regs& r) {
        Record({0x80D0, Mark(r.ecx), r.args[0], Mark(r.args[1])});
        return 0u;
    });
    s.At({0x0046CC50, AddressOf(&Script::CArray::SetSizeAndType)}, 8, [](Regs& r) {
        Record({0xCC50, Mark(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x0046CB10, AddressOf(&Script::CArray::SetElement)}, 8, [](Regs& r) {
        Record({0xCB10, Mark(r.ecx), r.args[0], Mark(r.args[1])});
        return 0u;
    });
    using GetStringFn = const char* (*)(uint32_t);
    s.At({0x00478FD0, AddressOf(static_cast<GetStringFn>(&Script::GetString))}, 0, [](Regs& r) {
        Record({0x8FD0, r.args[0]});
        return reinterpret_cast<uint32_t>("damaged");
    });
    using GetIntegerFn = int (*)(uint32_t);
    s.At({0x00478F60, AddressOf(static_cast<GetIntegerFn>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x8F60, r.args[0]});
        return 0x6000u;
    });
    using GetIntegerNamedFn = int (*)(const char*);
    s.At({0x004790C0, AddressOf(static_cast<GetIntegerNamedFn>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x90C0, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 7u;
    });
    s.At({0x0047A0C0, AddressOf(&Script::ReadFromBuffer)}, 0, [](Regs& r) {
        Record({0xA0C0, Mark(r.args[0]), r.args[1]});
        return r.args[1];
    });
    s.At({0x0047A070, AddressOf(&Script::CalculateBufferSize)}, 0, [](Regs& r) {
        Record({0xA070, Mark(r.args[0]), r.args[1]});
        return r.args[1] == 100 ? g_summarySize : g_mainSize;
    });
    s.At({0x00479E20, AddressOf(&Script::WriteToBuffer)}, 0, [](Regs& r) {
        Record({0x9E20, Mark(r.args[0]), Mark(r.args[1]), r.args[2], r.args[3], r.args[4] ? 1u : 0u});
        for (uint32_t i = 0; i < r.args[2]; ++i)
            reinterpret_cast<uint8_t*>(r.args[1])[i] = static_cast<uint8_t>(0x30 + i * 7 + r.args[2]);
        if (r.args[4])
            reinterpret_cast<void (*)()>(r.args[4])(); // the callback, as the writer calls it
        return r.args[2];
    });
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0x1C30, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    s.At({0x006234FE}, 0, StandInSprintf);
    s.At({0x00626EFB}, 0, [](Regs& r) {
        return static_cast<uint32_t>(_stricmp(reinterpret_cast<const char*>(r.args[0]), reinterpret_cast<const char*>(r.args[1])));
    });
    s.At({0x00452910, AddressOf(&Pcm::Update)}, 0, [](Regs&) { Record({0x2910}); return 0u; });
    s.At({0x0047B000, AddressOf(&Sfx::UpdateVoiceStates)}, 0, [](Regs&) { Record({0xB000}); return 0u; });
    s.At({0x00450F70, AddressOf(&Pcm::PauseMusic)}, 0, [](Regs& r) { Record({0x0F70, r.args[0]}); return 0u; });
    s.At({0x00451010, AddressOf(&Pcm::PauseStream)}, 0, [](Regs& r) { Record({0x1010, r.args[0]}); return 0u; });
    s.At({0x005591B0}, 0, [](Regs&) { Record({0x91B0}); return 0x60A10000u; });
    s.At({0x0055C090}, 0, [](Regs& r) { Record({0xC090, r.ecx}); return 0u; });
    s.At({0x005B5F10}, 0, [](Regs&) { Record({0x5F10}); return reinterpret_cast<uint32_t>("BASLUS-"); });
    // the save helpers: original register entry points, ours by their C++ arguments
    s.At({0x005A79A0}, 0, [](Regs& r) { Record({0x79A0, Mark(r.ebx), r.args[0]}); return 0u; });
    s.At({AddressOf(&Mc::WriteSaveContents)}, 0, [](Regs& r) { Record({0x79A0, Mark(r.args[0]), r.args[1]}); return 0u; });
    s.At({0x005A6610}, 0, [](Regs& r) { Record({0x6610, Mark(r.eax), Mark(r.esi), r.args[0]}); return 0u; });
    s.At({AddressOf(&Mc::WriteSummary)}, 0, [](Regs& r) {
        Record({0x6610, Mark(r.args[0]), Mark(r.args[1]), r.args[2]});
        return 0u;
    });
    s.At({0x005A7340}, 0, [](Regs& r) { Record({0x7340, r.ecx}); return g_fileSizeValue; });
    s.At({AddressOf(&Mc::TypeFileSize)}, 0, [](Regs& r) { Record({0x7340, r.args[0]}); return g_fileSizeValue; });
    s.At({0x005A63E0}, 0, [](Regs& r) {
        Record({0x63E0, Mark(r.edi), Mark(r.esi)});
        return YearOf(r.edi) > YearOf(r.esi) ? 1u : 0u;
    });
    s.At({AddressOf(&Mc::IsLater)}, 0, [](Regs& r) {
        Record({0x63E0, Mark(r.args[0]), Mark(r.args[1])});
        return YearOf(r.args[0]) > YearOf(r.args[1]) ? 1u : 0u;
    });
    s.At({0x005A8DA0, AddressOf(&Mc::CreateTemporaryMemCardPools)}, 0, [](Regs&) { Record({0x8DA0}); return 1u; });
    s.At({0x005A9700, AddressOf(&Mc::RemoveTemporaryMemCardPools)}, 0, [](Regs&) { Record({0x9700}); return 1u; });
    // the card and its files
    s.At({0x005BC730, AddressOf(&Mc::Manager::GetCard)}, 8, [](Regs& r) {
        Record({0xC730, r.args[0], r.args[1]});
        reinterpret_cast<int*>(g_card)[2] = g_cardError;
        return g_noCard ? 0u : reinterpret_cast<uint32_t>(g_card);
    });
    s.At({0x005BCAE0, AddressOf(&Mc::Card::GetFileList)}, 8, [](Regs& r) {
        Record({0xCAE0, Mark(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0]))});
        auto* head = reinterpret_cast<Mem::ListNode*>(r.args[1]);
        for (int i = 0; i < g_fileCount; ++i) {
            Mem::ListNode& node = g_files[i].file.m_node;
            node.vtable = g_nodeVtable;
            node.data = &g_files[i].file;
            node.prev = head->prev;
            node.next = head;
            head->prev->next = &node;
            head->prev = &node;
        }
        return 1u;
    });
    g_nodeVtable[0] = s.Thunk(4, [](Regs& r) { Record({0xDE1E, Mark(r.ecx), r.args[0]}); return r.ecx; });
    g_fileVtable[0] = s.Thunk(4, [](Regs& r) { Record({0xDE1F, Mark(r.ecx), r.args[0]}); return r.ecx; });
    s.At({0x005BC9B0, AddressOf(&Mc::Card::Open)}, 12, [](Regs& r) {
        const char* name = reinterpret_cast<const char*>(r.args[0]);
        Record({0xC9B0, Mark(r.ecx), Hash(name), r.args[1], r.args[2]});
        g_openIndex = -1;
        for (int i = 0; i < g_fileCount; ++i)
            if (std::strstr(name, g_files[i].file.m_name))
                g_openIndex = i;
        if (g_noFile || (r.args[1] == 1 && g_openIndex < 0) || (g_openIndex >= 0 && g_files[g_openIndex].header[0x77] == 1))
            return 0u;
        return reinterpret_cast<uint32_t>(g_openFile);
    });
    s.At({0x005BC800, AddressOf(&Mc::File::Seek)}, 8, [](Regs& r) {
        Record({0xC800, Mark(r.ecx), r.args[0], r.args[1]});
        return r.args[1] == 2 ? 0x9000u + static_cast<uint32_t>(g_openIndex) : 0u;
    });
    s.At({0x005BC880, AddressOf(&Mc::File::Read)}, 8, [](Regs& r) {
        Record({0xC880, Mark(r.ecx), r.args[0], r.args[1]});
        std::memcpy(reinterpret_cast<void*>(r.args[0]), g_files[g_openIndex].header, 0x78);
        return r.args[1];
    });
    s.At({0x005BC850, AddressOf(&Mc::File::Write)}, 8, [](Regs& r) {
        Record({0xC850, Mark(r.ecx), HashBytes(reinterpret_cast<const uint8_t*>(r.args[0]), r.args[1]), r.args[1]});
        bool shortWrite = (g_writeShort == 1 && r.args[1] > 0x14) || (g_writeShort == 2 && r.args[0] != 0 &&
                                                                          reinterpret_cast<uint8_t*>(r.args[0])[0] == 0x69);
        return shortWrite ? r.args[1] - 1 : r.args[1];
    });
    s.At({0x005BC840, AddressOf(&Mc::File::Flush)}, 0, [](Regs& r) { Record({0xC840, Mark(r.ecx)}); return g_flushFails ? 0u : 1u; });
    s.At({0x005BC8B0, AddressOf(&Mc::File::Close)}, 0, [](Regs& r) { Record({0xC8B0, Mark(r.ecx)}); return 1u; });
}

void AddFile(const char* name, int year, int variant)
{
    ListedFile& f = g_files[g_fileCount++];
    std::memset(&f, 0, sizeof(f));
    std::snprintf(f.file.m_name, sizeof(f.file.m_name), "%s", name);
    f.file.m_year = static_cast<uint16_t>(year);
    f.file.m_month = 5;
    f.file.m_day = static_cast<uint8_t>(year % 28);
    f.file.m_hour = 13;
    f.file.m_minute = 7;
    f.file.m_second = 42;
    auto* header = reinterpret_cast<uint32_t*>(f.header);
    for (int i = 0x14; i < 0x77; ++i)
        f.header[i] = static_cast<uint8_t>(i * 3);
    int summarySize = variant == 1 ? 101 : variant == 5 ? 100 : 0x40;
    header[2] = static_cast<uint32_t>(summarySize);
    header[1] = Crc::GenerateCRCFromBuffer(f.header + 0x14, summarySize > 100 ? 0 : summarySize) ^ (variant == 2 ? 1u : 0u);
    header[4] = variant == 3 ? 6u : 7u; // the version (the stand-in TypeVersion is 7)
    f.header[0x77] = variant == 4 ? 1 : 0; // won't open
}

void TestSaveToMemoryCard()
{
    std::vector<Saved> saved = {{0x007CEFA8, 4, {}}, {0x007D1890, 4, {}}, {0x007CECD0, 0x14, {}}, {0x007CEC58, 0x78, {}},
                                {0x006F2338, 16, {}}, {0x006F3614, 16, {}}, {0x006F3654, 16, {}}, {0x006F22B4, 16, {}},
                                {0x007CECE8, 0x100, {}}, {0x006F3624, 4, {}}};
    SaveAll(saved);
    UseEmptySymbolTable();
    auto* standIns = new StandIns;
    StandInCalls(*standIns);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        g_years.clear();
        g_arena = g_savingArenas[run];
        g_arenaUsed = 0;
        GameVariable<uint32_t>(0x007D1890) = 0x3C000000;
        std::memset(g_openFile, 0, sizeof(g_openFile));
        *reinterpret_cast<void***>(g_openFile) = g_fileVtable;
        auto call = [&](uint32_t address, CFunc ours, Params& params) { Call(original, address, ours, params); };

        // the callback
        if (original)
            Original<void (*)()>(0x005A7420)();
        else
            Mc::KeepSoundGoing();

        // GetMemCardDirectoryListing: each platform, each wanted type, the temporary pools there or not
        for (int platform : {4, 2, 5, 6, 8}) {
            GameVariable<int>(0x007CEFA8) = platform;
            g_fileCount = 0;
            if (platform == 4) {
                AddFile("BASLUS-park1.PRK", 2003, 0);
                AddFile("BASLUS-opts.ska", 2005, 0);
                AddFile("BASLUS-bad.PRK", 2004, 1);
                AddFile("BASLUS-crc.CAT", 2006, 2);
                AddFile("BASLUS-ver.NWS", 2002, 3);
                AddFile("BASLUS-gone.GOA", 2007, 4);
                AddFile("other.PRK", 2001, 0);
                AddFile("BASLUS-x.TXT", 2008, 0);
                AddFile("BASLUSXpark.PRK", 2010, 0);
                AddFile("BASLUS-full.PRK", 2011, 5);
            } else {
                AddFile("BASLUS-abcdefgc", 2003, 0);
                AddFile("BASLUS-abcdefga", 2009, 0);
                AddFile("BASLUS-abcdefgf", 2004, 1);
                AddFile("BASLUS-abcdefgd", 2005, 0);
            }
            for (uint32_t wanted : {0u, Mc::kSavePark}) {
                for (bool temporary : {false, true}) {
                    std::memset(reinterpret_cast<void*>(0x006F2338), 0, 16);
                    std::memset(reinterpret_cast<void*>(0x006F3614), 0, 16);
                    GameVariable<uint32_t>(0x006F3614 + 8) = 0x0B0B0000;
                    GameVariable<uint32_t>(0x006F3614 + 12) = 0x0B0B1000;
                    GameVariable<uint32_t>(0x006F22B4 + 8) = 0x0B0C0000;
                    if (temporary)
                        GameVariable<uint32_t>(0x006F2338 + 12) = 0x0B0D0000;
                    Params p;
                    if (wanted)
                        p.Checksum(0x11093FB5, wanted);
                    call(0x005A8F70, Mc::GetMemCardDirectoryListing, p);
                    Record({GameVariable<uint32_t>(0x006F2338), GameVariable<uint32_t>(0x006F3614)});
                }
            }
        }
        // the limits: 75 of the game's files (none listed), 76 of the wanted type (75 listed)
        GameVariable<int>(0x007CEFA8) = 4;
        for (int files : {75, 76}) {
            g_fileCount = 0;
            for (int i = 0; i < files; ++i) {
                char name[32];
                std::snprintf(name, sizeof(name), "BASLUS-%02d%s", i, files == 75 ? ".TXT" : ".PRK");
                AddFile(name, 1990 + i % 7, 0);
            }
            Params p;
            if (files == 75)
                p.Checksum(0x11093FB5, Mc::kSavePark);
            call(0x005A8F70, Mc::GetMemCardDirectoryListing, p);
        }
        g_fileCount = 0;
        // no card
        g_noCard = true;
        {
            Params p;
            call(0x005A8F70, Mc::GetMemCardDirectoryListing, p);
        }
        // SaveToMemoryCard: each platform; vault data or not; sizes with and without padding; failures
        g_fileCount = 0;
        GameVariable<uint32_t>(0x007CECD4) = 0x0CEC0000;
        GameVariable<uint32_t>(0x007CECD8) = Mc::kSaveCat;
        struct Case {
            int platform;
            bool vault;
            uint32_t type;
            int failure; // 0 none, 1 no card, 2 no file, 3 header write short, 4 padding write short, 5 flush, 6 full
            uint32_t fileSize;
        };
        const Case cases[] = {
            {4, false, Mc::kSavePark, 0, 0x200},           {4, false, Mc::kSaveOptionsAndPros, 0, 0x200},
            {4, false, Mc::kSaveNetworkSettings, 0, 0x200}, {4, true, Mc::kSavePark, 0, 0x200},
            {4, false, Mc::kSavePark, 0, 0x14 + 0x50 + 0x20}, {4, false, Mc::kSavePark, 1, 0x200},
            {4, false, Mc::kSavePark, 2, 0x200},           {4, false, Mc::kSavePark, 3, 0x200},
            {4, false, Mc::kSavePark, 4, 0x200},           {4, false, Mc::kSavePark, 5, 0x200},
            {4, false, Mc::kSavePark, 6, 0x200},           {7, false, Mc::kSaveCat, 0, 0x200},
            {5, false, Mc::kSavePark, 0, 0x200},           {2, false, Mc::kSavePark, 0, 0x200},
            {6, false, Mc::kSavePark, 0, 0x200},           {4, false, Mc::kSavePark, 7, 0x200},
        };
        for (const Case& c : cases) {
            GameVariable<int>(0x007CEFA8) = c.platform;
            g_noCard = c.failure == 1;
            g_noFile = c.failure == 2 || c.failure == 7;
            g_writeShort = c.failure == 3 ? 1 : c.failure == 4 ? 2 : 0;
            g_flushFails = c.failure == 5;
            g_cardError = c.failure == 6 || c.failure == 7 ? 2 : 0;
            g_fileSizeValue = c.fileSize;
            g_mainSize = 0x50;
            g_summarySize = 0x20;
            for (bool temporary : {false, true}) {
                std::memset(reinterpret_cast<void*>(0x006F2338), 0, 16);
                std::memset(reinterpret_cast<void*>(0x006F3614), 0, 16);
                std::memset(reinterpret_cast<void*>(0x006F3654), 0, 16);
                GameVariable<uint32_t>(0x006F3614 + 8) = 0x0B0B0000;
                GameVariable<uint32_t>(0x006F3614 + 12) = 0x0B0B1000;
                if (temporary)
                    GameVariable<uint32_t>(0x006F2338 + 12) = 0x0B0D0000;
                GameVariable<uint8_t>(0x007CECD0) = 0x55;
                Params p;
                p.String(Crc::GenerateCRCFromString("Name"), "My Save").Checksum(Crc::GenerateCRCFromString("Type"), c.type);
                if (c.vault)
                    p.Integer(0xBB609E73, 1);
                call(0x005A97E0, Mc::SaveToMemoryCard, p);
                Record({GameVariable<uint8_t>(0x007CECD0), GameVariable<uint32_t>(0x006F2338), GameVariable<uint32_t>(0x006F3614),
                        GameVariable<uint32_t>(0x006F3654)});
            }
        }
        g_noCard = g_noFile = g_flushFails = false;
        g_writeShort = 0;
        g_cardError = 0;
        for (uint32_t& word : g_trace)
            word = Mark(word);
        for (uint32_t i = 0; i < g_arenaUsed / 4; ++i)
            g_trace.push_back(ArenaWordOrHigh(g_savingArenas, kArenaSize, reinterpret_cast<uint32_t*>(g_arena)[i]));
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(saved);
    Check(traces[0].size() > 1500, "saving: the original reached its calls");
    Compare(traces[0], traces[1], "directory listing and saving (0x005A7420, 0x005A8F70, 0x005A97E0)");
}

} // namespace

SELFTEST(Early, TestSaveToMemoryCard, RemoveStandIns);
