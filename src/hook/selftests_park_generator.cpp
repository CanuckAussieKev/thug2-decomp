// Self-tests for the park's generator (first part) and the metapiece lists (src/game/ed/park_generator.cpp): the
// original and ours on the same stand-in generator, pieces and park manager (park_pieces_testkit.h), the scenes,
// memory, park memory figures, script structures and arrays stood in for; calls, results and the whole arena compared.

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/ass/asset.h"
#include "game/core/pool.h"
#include "game/ed/park.h"
#include "game/ed/park_scene.h"
#include "game/park/park_memory.h"
#include "game/script/struct.h"
#include "hook/park_pieces_testkit.h"

namespace {

using namespace ParkPiecesTestKit;
using Ed::CMapListNode;
using Ed::CMapListTemp;
using Ed::CParkGenerator;
using Ed::CPiece;
using Ed::GridDims;

constexpr uint32_t kManager = 0x4000;   // the park manager (+0x3520 its bounds)
constexpr uint32_t kGenerator = 0x2000; // 0x4A4 bytes
constexpr uint32_t kNodes = 0x0800;     // map list nodes (0x10 apart)
constexpr uint32_t kMetas = 0x0900;     // metapieces for the list (0x40 apart)
constexpr uint32_t kOut = 0x0C00;
constexpr uint32_t kArray = 0x0C80;     // a script array (+4 its size)
constexpr uint32_t kStructs = 0x0D00;   // its structures (0x10 apart, only addresses)
constexpr uint32_t kPieces = 0x1400;    // cloned pieces, 0x30 apart
constexpr uint32_t kSources = 0x1800;   // source pieces, 0x30 apart
constexpr uint32_t kSourceScene = 0x5000;
constexpr uint32_t kParkScene = 0x5100;
constexpr uint32_t kShellScene = 0x5200;

int g_mainHeapFree;
bool g_deleted;
bool g_haveShell;
uint32_t g_clones;
std::map<std::pair<uint32_t, uint32_t>, uint32_t> g_checksums; // (structure, name) -> value
std::map<std::pair<uint32_t, uint32_t>, bool> g_flags;

void Install(StandIns& s)
{
    InstallScene(s);
    s.At({0x0059E730, AddressOf(&Ed::GetMemoryStats)}, 0, [](Regs& r) {
        Rec(0xE730, {}); // (its buffer is on the stack)
        auto* stats = reinterpret_cast<Ed::MemoryStats*>(r.args[0]);
        std::memset(stats, 0x5A, sizeof(Ed::MemoryStats));
        stats->mainHeapFree = g_mainHeapFree;
        return r.args[0];
    });
    // The list nodes' pools: the first has items at +0x30 of the nodes, the second at +0x40.
    s.At({0x005BA3B0, AddressOf(&Mem::Pool::Contains)}, 4, [](Regs& r) {
        Rec(0xA3B0, {r.ecx, r.args[0]});
        uint32_t item = r.args[0] - At(kNodes);
        return (r.ecx == At(0x7A00) && item == 0x30) || (r.ecx == At(0x7A40) && item == 0x40) ? 1u : 0u;
    });
    s.At({0x005BA400, AddressOf(&Mem::Pool::Free)}, 4, [](Regs& r) {
        Rec(0xA400, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x0048C700}, 0, [](Regs& r) {
        Rec(0xC700, {r.args[0]});
        return g_haveShell ? At(kShellScene) : 0u;
    });
    s.At({0x0049DF30}, 0x14, [](Regs& r) {
        Rec(0xDF30, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3], r.args[4]});
        return 0xC000 + ++g_clones;
    });
    s.At({0x0049CF80}, 4, [](Regs& r) {
        Rec(0xCF80, {r.ecx, r.args[0]});
        return g_deleted ? 1u : 0u;
    });
    s.At({0x0049D780}, 4, [](Regs& r) {
        Rec(0xD780, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x0049CE60}, 4, [](Regs& r) {
        Rec(0xCE60, {r.ecx});
        RecWords(0xCE61, r.args[0], 8);
        return 0u;
    });
    s.At({0x0049D0B0}, 12, [](Regs& r) {
        Rec(0xD0B0, {r.ecx, r.args[1], r.args[2]});
        RecWords(0xD0B1, r.args[0], 8);
        return 0u;
    });
    s.At({0x00595170, AddressOf(&Ed::CParkManager::GetShellChecksum)}, 0, [](Regs& r) {
        Rec(0x5170, {r.ecx});
        return 0x5E11u;
    });
    s.At({0x00595300, AddressOf(&Ed::CParkManager::GetRestartIndex)}, 4, [](Regs& r) {
        Rec(0x5300, {r.ecx, r.args[0]});
        return r.args[0] & 1;
    });
    s.At({0x00595A90, AddressOf(&Ed::CParkManager::GridCoordinatesToWorld)}, 12, [](Regs& r) {
        const auto* area = reinterpret_cast<const uint8_t*>(r.args[1]);
        Rec(0x5A90, {r.ecx, r.args[2], *reinterpret_cast<const uint32_t*>(area), *reinterpret_cast<const uint16_t*>(area + 4)});
        auto* out = reinterpret_cast<float*>(r.args[0]);
        out[0] = area[0] * 120.0f - 3480.25f;
        out[1] = static_cast<int8_t>(area[1]) * 48.0f;
        out[2] = area[2] * 120.0f - 3480.75f;
        out[3] = 1.0f;
        return r.args[0];
    });
    s.At({0x0059E100, AddressOf(&Ed::GetParkHeap)}, 0, [](Regs&) {
        Rec(0xE100, {});
        return At(0x7900);
    });
    s.At({0x0059E170, AddressOf(&Ed::SetCountsDirty)}, 0, [](Regs&) {
        Rec(0xE170, {});
        return 0u;
    });
    s.At({0x00407810, AddressOf(&Ass::CAsset::Destruct)}, 0, [](Regs& r) {
        Rec(0x7810, {r.ecx});
        return 0u;
    });
    s.At({0x0058B700, AddressOf(&Ed::CParkRailSet::Clear)}, 0, [](Regs& r) {
        Rec(0xB700, {r.ecx});
        return 0u;
    });
    s.At({0x0058B280, AddressOf(&Ed::CParkRailSet::FreePools)}, 0, [](Regs& r) {
        Rec(0xB280, {r.ecx});
        return 0u;
    });
    s.At({0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, [](Regs& r) {
        Rec(0xCC30, {r.ecx, r.args[0]});
        return At(kStructs) + r.args[0] * 0x10;
    });
    s.At({0x00476950, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, uint32_t*, int)>(&Script::CStruct::GetChecksum))},
         12, [](Regs& r) {
             Rec(0x6950, {r.ecx, r.args[0], r.args[2] & 0xFF, *reinterpret_cast<uint32_t*>(r.args[1])});
             auto it = g_checksums.find({r.ecx - g_base, r.args[0]});
             if (it == g_checksums.end())
                 return 0u;
             *reinterpret_cast<uint32_t*>(r.args[1]) = it->second;
             return 1u;
         });
    s.At({0x00476B40, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t)>(&Script::CStruct::ContainsFlag))}, 4,
         [](Regs& r) {
             Rec(0x6B40, {r.ecx, r.args[0]});
             return g_flags[{r.ecx - g_base, r.args[0]}] ? 1u : 0u;
         });
}

void SetUp(int run)
{
    SetUpArena(run);
    SetUpGeometry();
    g_geometry = At(kGeometry);
    g_clones = 0;
    *Ptr<uint32_t>(kManager) = At(kGenerator);
    *Ptr<GridDims>(kManager + 0x3520) = {9, -1, 11, 21, 30, 17};
    GameVariable<uint32_t>(0x007CE604) = At(kManager);
    *Ptr<uint32_t>(0x7000 + 0x114) = At(0x7800); // the memory manager's bottom-up heap
    std::memset(reinterpret_cast<void*>(0x007CE5F0), 0, 16); // the list nodes' pools: none
    GameVariable<bool>(0x0067F398) = true;
    GameVariable<int>(0x007CE614) = 0x77;
    // The generator: dirty, then its lists and scenes.
    auto* generator = Ptr<CParkGenerator>(kGenerator);
    for (uint32_t i = 0; i < 0x4A4; i += 4)
        *Ptr<uint32_t>(kGenerator + i) = 0x11110000 + i;
    Ed::Data(generator).m_flags = 0x26;
    Ed::Data(generator).mp_sourceScene = Ptr<uint8_t>(kSourceScene);
    Ed::Data(generator).mp_scene = Ptr<uint8_t>(kParkScene);
    Ed::Data(generator).m_nextPieceId = 40;
    Ed::Data(generator).m_maxPlayers = 2;
    // Four sources (listed), five cloned pieces of them (listed: 0..4).
    for (uint32_t i = 0; i < 4; ++i) {
        MakeSource(kSources + i * 0x30, 0x5000 + i, 120.0f * (i + 1), 48.0f, 120.0f);
        Ptr<Ed::CSourcePiece>(kSources + i * 0x30)->mp_next = i < 3 ? Ptr<Ed::CSourcePiece>(kSources + (i + 1) * 0x30) : nullptr;
    }
    Ed::Data(generator).mp_sources = Ptr<Ed::CSourcePiece>(kSources);
    for (uint32_t i = 0; i < 5; ++i) {
        MakeClone(kPieces + i * 0x30, kSources + (i % 4) * 0x30, 0x7000 + i * 0x10, static_cast<int>(i), 0.0f, 0.0f, 0.0f);
        auto* piece = Ptr<CPiece>(kPieces + i * 0x30);
        piece->m_flags = CPiece::kCloned | CPiece::kInList | (i & 1 ? CPiece::kGap : 0u);
        piece->mp_next = i < 4 ? Ptr<CPiece>(kPieces + (i + 1) * 0x30) : nullptr;
    }
    Ed::Data(generator).mp_pieces = Ptr<CPiece>(kPieces);
}

void TestParkGenerator1()
{
    SaveGlobals({{0x007CE604, 4, {}}, {0x007CE5F0, 16, {}}, {0x0067F398, 1, {}}, {0x007CE614, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUp(run);
        auto* generator = Ptr<CParkGenerator>(kGenerator);
        // A list of three metapieces: printed, checked, freed (node by node and as a whole).
        g_printfArgs = 6;
        for (uint32_t i = 0; i < 3; ++i) {
            auto* node = Ptr<CMapListNode>(kNodes + i * 0x10);
            node->m_dy = static_cast<int>(i);
            node->mp_meta = Ptr<Ed::CMetaPiece>(kMetas + i * 0x40);
            node->mp_next = i < 2 ? Ptr<CMapListNode>(kNodes + (i + 1) * 0x10) : nullptr;
            node->mp_meta->m_cell_area = {static_cast<uint8_t>(i), static_cast<int8_t>(-1 - static_cast<int>(i)), 7, 1, 2, 3};
        }
        auto* holder = Ptr<CMapListTemp>(kOut);
        holder->list = Ptr<CMapListNode>(kNodes);
        original ? Orig<void>(0x00591450, holder) : holder->PrintContents();
        if (original) {
            uint32_t nodes = At(kNodes);
            __asm {
                mov ecx, nodes
                mov eax, 0x00590470
                call eax
            }
        } else {
            CMapListNode::VerifyIntegrity(Ptr<CMapListNode>(kNodes));
        }
        // Freed into a pool (the current one or the other has it) or to the memory manager.
        GameVariable<uint32_t>(0x007CE5F8) = At(0x7A00);
        GameVariable<uint32_t>(0x007CE5FC) = At(0x7A40);
        for (uint32_t at : {0x30u, 0x40u, 0x50u, 0x20u}) {
            if (at == 0x20)
                GameVariable<uint32_t>(0x007CE5F8) = 0;
            original ? Original<void(__cdecl*)(void*)>(0x005914B0)(Ptr<void>(kNodes + at)) : CMapListNode::Free(Ptr<void>(kNodes + at));
        }
        std::memset(reinterpret_cast<void*>(0x007CE5F0), 0, 16);
        Ptr<CMapListNode>(kNodes + 0x10)->mp_next = nullptr;
        holder->list = nullptr;
        original ? Orig<void>(0x00591C60, holder) : holder->Destroy();
        holder->list = Ptr<CMapListNode>(kNodes);
        original ? Orig<void>(0x00591C60, holder) : holder->Destroy();
        PushArena(t);
        // Players, sources, sectors, rails, lights.
        for (int players : {1, 2, 8, 0}) {
            for (int free : {0, 585499, 585500, 1171000, 7000000, -5}) {
                Ed::Data(generator).m_maxPlayers = players;
                g_mainHeapFree = free;
                t.push_back(static_cast<uint32_t>(original ? Orig<int>(0x00591D30, generator) : generator->GetMaxPlayersPossible()));
            }
        }
        original ? Orig<void>(0x00591D20, generator, 6) : generator->SetMaxPlayers(6);
        for (uint32_t name : {0x5000u, 0x5003u, 0x5002u, 0x4FFFu}) {
            void* source = original ? Orig<void*>(0x00591D90, generator, name) : generator->GetPieceSource(name);
            t.push_back(Rel(reinterpret_cast<uint32_t>(source)));
        }
        for (bool deleted : {true, false}) {
            g_deleted = deleted;
            t.push_back(original ? Orig<bool>(0x00591DD0, generator, 0xABCu, 7) : generator->DeleteSector(0xABC, 7));
        }
        original ? Orig<void>(0x00591DE0, generator) : generator->DestroyRails();
        original ? Orig<void>(0x00591E00, generator, 5, 0.5f, 0.25f, 0.125f, 1.5f, 2.5f, 3.5f, 0.75f)
                 : generator->SetLights(5, 0.5f, 0.25f, 0.125f, 1.5f, 2.5f, 3.5f, 0.75f);
        PushArena(t);
        // The restart slots: the range of each type, whether a slot is free, cleared.
        for (int i = 0; i < Ed::ParkGeneratorData::kNumRestarts; ++i) {
            Ed::Data(generator).m_restarts[i].m_type = i % 3 ? 4 : 0;
            Ed::Data(generator).m_restarts[i].m_automatic = i % 4 == 1;
        }
        Ed::Data(generator).m_restarts[17].m_type = 1;
        for (int type = 0; type < 10; ++type) {
            int range[2] = {-1, -1};
            original ? Orig<void>(0x00591E60, generator, type, &range[0], &range[1])
                     : generator->GetRestartSlots(type, &range[0], &range[1]);
            t.push_back(static_cast<uint32_t>(range[0]));
            t.push_back(static_cast<uint32_t>(range[1]));
            t.push_back(original ? Orig<bool>(0x00591F50, generator, type) : generator->HasFreeRestartSlot(type));
        }
        original ? Orig<void>(0x00591FE0, generator) : generator->ClearAutomaticRestarts();
        PushArena(t);
        for (int type = 0; type < 10; ++type)
            t.push_back(original ? Orig<bool>(0x00591F50, generator, type) : generator->HasFreeRestartSlot(type));
        original ? Orig<void>(0x00591FC0, generator) : generator->ClearRestarts();
        PushArena(t);
        original ? Orig<void>(0x00591C70, generator) : generator->DestroyBody();
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "park generator: lists, players, sources, lights, restart slots, destructor");
}

void TestParkGenerator2()
{
    SaveGlobals({{0x007CE604, 4, {}}, {0x007CE5F0, 16, {}}, {0x0067F398, 1, {}}, {0x007CE614, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUp(run);
        auto* generator = Ptr<CParkGenerator>(kGenerator);
        // The scene's bounds, the collision (allowed or not), the shell's boxes (shell or none).
        original ? Orig<void>(0x00592010, generator) : generator->SetUpBounds();
        for (bool rebuild : {true, false}) {
            GameVariable<bool>(0x0067F398) = rebuild;
            original ? Orig<void>(0x00592090, generator) : generator->RebuildCollision();
            for (bool deleted : {true, false}) {
                g_deleted = deleted;
                t.push_back(original ? Orig<bool>(0x00592370, generator, 0x7010u, 3) : generator->DestroySector(0x7010, 3));
            }
            t.push_back(Ed::Data(generator).m_flags);
        }
        GameVariable<bool>(0x0067F398) = true;
        for (bool shell : {true, false}) {
            g_haveShell = shell;
            original ? Orig<void>(0x005920D0, generator, 0x99u) : generator->UpdateBounds(0x99);
        }
        // Shown, active, tinted (each piece's next read first).
        for (int on : {1, 0}) {
            original ? Orig<void>(0x00592290, generator, on) : generator->SetPiecesHighlighted(on);
            original ? Orig<void>(0x005922D0, generator, on != 0) : generator->SetRestartPiecesActive(on != 0);
            original ? Orig<void>(0x00591CF0, generator, on != 0) : generator->HighlightAllPieces(on != 0);
        }
        uint32_t clone = original ? Orig<uint32_t>(0x00592320, generator, 0x5002u, 1, 0, 9) : generator->CloneSector(0x5002, 1, 0, 9);
        t.push_back(clone);
        PushArena(t);
        // Pieces made (from a source, from a cloned piece's source; listed or not; flags masked), added.
        const uint32_t flags[] = {0x0u, 0x4u, 0xFFFFFFFFu, 0x320u, 0x24u};
        for (int i = 0; i < 5; ++i) {
            CPiece* source = Ptr<CPiece>(i & 1 ? kPieces + 0x30 * static_cast<uint32_t>(i) : kSources + 0x30 * static_cast<uint32_t>(i % 4));
            CPiece* piece = original ? Orig<CPiece*>(0x005923C0, generator, source, flags[i]) : generator->CreatePiece(source, flags[i]);
            t.push_back(Rel(reinterpret_cast<uint32_t>(piece)));
            t.push_back(static_cast<uint32_t>(GameVariable<int>(0x007CE614)));
            if (i != 2)
                original ? Orig<void>(0x00592550, generator, piece) : generator->AddPiece(piece);
            PushArena(t);
        }
        // Pieces deleted: one that isn't listed (flag 4: left in the list), the first, a middle one, one that's listed
        // but not in the list, then all the rest.
        Ptr<CPiece>(kPieces + 0x60)->m_flags |= CPiece::kCursor;
        MakeClone(kPieces + 0x150, kSources, 0x7090, 0, 0.0f, 0.0f, 0.0f);
        const uint32_t order[] = {0x60, 0x00, 0x90, 0x150, 0x30, 0xC0};
        int keep = 0;
        for (uint32_t at : order) {
            original ? Orig<void>(0x00592570, generator, Ptr<CPiece>(kPieces + at), keep) : generator->DeletePiece(Ptr<CPiece>(kPieces + at), keep);
            keep ^= 1;
            PushArena(t);
        }
        for (CPiece* piece = Ed::Data(generator).mp_pieces; piece; piece = piece->mp_next)
            piece->m_flags &= ~CPiece::kCursor;
        while (CPiece* piece = Ed::Data(generator).mp_pieces) {
            original ? Orig<void>(0x00592570, generator, piece, 0) : generator->DeletePiece(piece, 0);
            t.push_back(Ed::Data(generator).m_flags);
        }
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "park generator: scene bounds, collision, shown, pieces made, added, deleted");
}

void TestParkGenerator3()
{
    SaveGlobals({{0x007CE604, 4, {}}, {0x007CE5F0, 16, {}}, {0x0067F398, 1, {}}, {0x007CE614, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUp(run);
        auto* generator = Ptr<CParkGenerator>(kGenerator);
        // The piece list: types (the two that count, another, none), names (a source, none, unknown), rails with and
        // without the flag, the piece types (four floor-locked ones, another, none).
        struct Entry {
            uint32_t type, name, pieceType;
            bool flag;
        };
        const Entry entries[] = {
            {0xBF4D3536, 0x5000, 0x3DEDAFE9, false}, {0xEF59C100, 0x5001, 0x22C20ED8, true},
            {0xEF59C100, 0x5002, 0x12345678, false}, {0xBF4D3536, 0x5003, 0, false},
            {0x11111111, 0x5000, 0x28673697, true},  {0xBF4D3536, 0, 0xF6187E48, false},
            {0xEF59C100, 0x4444, 0xF6187E48, true},  {0xBF4D3536, 0x5001, 0, false}, {0, 0x5001, 0, false},
            {0xBF4D3536, 0x5002, 0x28673697, false}, {0xEF59C100, 0x5003, 0xF6187E48, true},
        };
        Ptr<Ed::CSourcePiece>(kSources + 0x90)->m_name = 0; // (a source named 0: found only without the name check)
        for (uint32_t i = 0; i < 4; ++i)
            Ptr<Ed::CSourcePiece>(kSources + i * 0x30)->m_triggerScript = 0x77;
        g_checksums.clear();
        g_flags.clear();
        uint32_t count = 0;
        for (const Entry& e : entries) {
            uint32_t at = kStructs + count * 0x10;
            if (e.type)
                g_checksums[{at, 0x12B4E660}] = e.type;
            if (e.name)
                g_checksums[{at, 0xA1DC81F9}] = e.name;
            if (e.pieceType)
                g_checksums[{at, 0x2CA8A299}] = e.pieceType;
            g_flags[{at, 0x68910AC6}] = e.flag;
            ++count;
        }
        *Ptr<uint32_t>(kArray + 4) = count;
        original ? Orig<void>(0x005925E0, generator, Ptr<Script::CArray>(kArray))
                 : generator->ReadPieceFlags(Ptr<Script::CArray>(kArray));
        PushArena(t);
        *Ptr<uint32_t>(kArray + 4) = 0;
        original ? Orig<void>(0x005925E0, generator, Ptr<Script::CArray>(kArray))
                 : generator->ReadPieceFlags(Ptr<Script::CArray>(kArray));
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "park generator: the piece list's flags and types");
}

} // namespace

SELFTEST(Early, TestParkGenerator1, ViewerTestKit::Cleanup);
SELFTEST(Early, TestParkGenerator2, ViewerTestKit::Cleanup);
SELFTEST(Early, TestParkGenerator3, ViewerTestKit::Cleanup);
