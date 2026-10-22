// Self-tests for the park editor's metapieces (src/game/ed/park_metapieces.cpp): the original and ours on the same
// stand-in metapieces and pieces (park_pieces_testkit.h), the park manager's palette lookup, the generator's sources,
// the sectors, matrices, memory and script structures stood in for; calls, results and the whole arena compared.

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/ed/park.h"
#include "game/script/struct.h"
#include "hook/park_pieces_testkit.h"

namespace {

using namespace ParkPiecesTestKit;
using Ed::CAbstractMetaPiece;
using Ed::CConcreteMetaPiece;
using Ed::CMetaPiece;
using Ed::CPiece;
using Ed::GridDims;
using Ed::SMetaDescriptor;

constexpr uint32_t kManager = 0x0800;   // the park manager (only +0x00, its generator)
constexpr uint32_t kGenerator = 0x0880;
constexpr uint32_t kMeta = 0x1000;      // the metapiece under test (0x3C)
constexpr uint32_t kExtra = 0x1080;     // its extra descriptors (8)
constexpr uint32_t kOut = 0x1100;
constexpr uint32_t kArray = 0x1180;
constexpr uint32_t kPieces = 0x1400;    // cloned pieces, 0x30 apart
constexpr uint32_t kSources = 0x1800;   // source pieces, 0x30 apart
constexpr uint32_t kPalette = 0x2000;   // abstract metapieces, 0x80 apart (their extra descriptors at +0x40)

std::map<uint32_t, uint32_t> g_palette; // name -> abstract metapiece

void Install(StandIns& s)
{
    InstallScene(s);
    s.At({0x00595620, AddressOf(&Ed::CParkManager::GetAbstractMetaPiece)}, 8, [](Regs& r) {
        Rec(0x5620, {r.ecx, r.args[0], r.args[1]});
        auto it = g_palette.find(r.args[0]);
        return it == g_palette.end() ? 0u : it->second;
    });
    s.At({0x00591D90, AddressOf(&Ed::CParkGenerator::GetPieceSource)}, 4, [](Regs& r) {
        Rec(0x1D90, {r.ecx, r.args[0]});
        return At(kSources) + (r.args[0] & 3) * 0x30;
    });
    s.At({0x00477E20, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, float, float, float)>(&Script::CStruct::AddVector))},
         16, [](Regs& r) {
             Rec(0x7E20, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3]});
             return 0u;
         });
    s.At({0x0046CC50, AddressOf(&Script::CArray::SetSizeAndType)}, 8, [](Regs& r) {
        Rec(0xCC50, {r.ecx, r.args[0], r.args[1] & 0xFF});
        return 0u;
    });
    s.At({0x0046CB10, AddressOf(&Script::CArray::SetElement)}, 8, [](Regs& r) {
        Rec(0xCB10, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
}

void SetUp(int run)
{
    SetUpArena(run);
    SetUpGeometry();
    g_geometry = At(kGeometry);
    *Ptr<uint32_t>(kManager) = At(kGenerator);
    GameVariable<uint32_t>(0x007CE604) = At(kManager);
    for (uint32_t i = 0; i < 4; ++i)
        MakeSource(kSources + i * 0x30, 0x5000 + i, 119.1234f * (i + 1), i == 3 ? 0.0f : 48.0f * (i + 1) - 1.5f, 238.7777f - 0.6113f * i);
}

// A metapiece at `at`: `count` descriptors (the first two inline, the rest at at + 0x80 or `extra`).
CMetaPiece* MakeMeta(uint32_t at, uint32_t vtable, uint32_t flags, uint32_t count, uint32_t extra = 0)
{
    auto* meta = Ptr<CMetaPiece>(at);
    std::memset(meta, 0, sizeof(CMetaPiece));
    meta->m_vtable = reinterpret_cast<void**>(vtable);
    meta->m_flags = flags;
    meta->m_cell_area = {3, -2, 5, 4, 2, 3};
    meta->m_max_entries = count > 2 ? count + 1 : 2;
    meta->m_num_used_entries = count;
    meta->mp_extra_desc = count > 2 ? Ptr<SMetaDescriptor>(extra ? extra : at + 0x40) : nullptr;
    meta->m_name_checksum = 0x6000 + at;
    return meta;
}

// Descriptor i of `meta`: at (x, y, z) turned `rot`, its piece `piece`.
void SetEntry(CMetaPiece* meta, int i, uint8_t x, int8_t y, uint8_t z, uint8_t rot, bool flagged, uint32_t piece)
{
    SMetaDescriptor* d = i < 2 ? &meta->m_desc[i] : &meta->mp_extra_desc[i - 2];
    d->x = x;
    d->y = y;
    d->z = z;
    d->rot = rot;
    d->isReal = flagged;
    d->piece = reinterpret_cast<void*>(piece);
}

// A concrete metapiece of five cloned pieces.
CMetaPiece* MakeConcrete()
{
    CMetaPiece* meta = MakeMeta(kMeta, CConcreteMetaPiece::kVtable, CMetaPiece::kConcrete, 5, kExtra);
    for (int i = 0; i < 5; ++i) {
        MakeClone(kPieces + i * 0x30, kSources + (i % 4) * 0x30, 0x7000 + i * 0x10, i, 100.0f * i - 230.5f, 48.0f * i,
                  -61.25f * i + 17.0f);
        SetEntry(meta, i, static_cast<uint8_t>(i), static_cast<int8_t>(i == 3 ? -1 : i % 2), static_cast<uint8_t>(4 - i),
                 static_cast<uint8_t>(i % 4), i == 1 || i == 4, At(kPieces + i * 0x30));
    }
    SetEntry(meta, 5, 9, 9, 9, 1, true, 0x1234); // (past the end)
    return meta;
}

void TestParkMetaPieces1()
{
    SaveGlobals({{0x007CE604, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUp(run);
        CMetaPiece* meta = MakeConcrete();
        // The restart pieces by name.
        for (uint32_t name : {0xCA07B4FCu, 0x613C7766u, 0x3A784D4Cu, 0x4E2EFD2Au, 0x8A1576B6u, 0xD7EB62B5u, 0xDC09A6A4u,
                              0xEC82433Cu, 0xEC82433Du, 0u, 0x613C7765u}) {
            meta->m_name_checksum = name;
            t.push_back(original ? Orig<bool>(0x005902F0, meta) : meta->IsRestartPiece());
        }
        for (int i = -1; i < 7; ++i) {
            void* d = original ? Orig<void*>(0x00590340, meta, i) : meta->GetDescriptor(i);
            t.push_back(Rel(reinterpret_cast<uint32_t>(d)));
            void* p = original ? Orig<void*>(0x00590360, meta, static_cast<uint32_t>(i))
                               : static_cast<CConcreteMetaPiece*>(meta)->GetPiece(static_cast<uint32_t>(i));
            t.push_back(Rel(reinterpret_cast<uint32_t>(p)));
        }
        // Within an area (x and z only).
        const GridDims areas[] = {{3, 0, 5, 4, 1, 3}, {2, 0, 4, 6, 1, 5}, {4, 0, 5, 4, 1, 3}, {3, 0, 6, 4, 1, 3},
                                  {3, 0, 5, 3, 1, 3}, {3, 0, 5, 4, 1, 2}, {0, 0, 0, 255, 0, 255}, {3, 9, 5, 4, 0, 3}};
        for (const GridDims& area : areas) {
            *Ptr<GridDims>(kOut) = area;
            t.push_back(original ? Orig<bool>(0x00590750, meta, Ptr<GridDims>(kOut))
                                 : meta->IsCompletelyWithinArea(Ptr<GridDims>(kOut)));
        }
        // Shown, hidden, highlighted (a piece that isn't cloned skipped).
        Ptr<CPiece>(kPieces + 0x60)->m_flags = CPiece::kSource;
        for (int on : {1, 0})
            original ? Orig<void>(0x005903A0, meta, on != 0) : meta->SetVisibility(on != 0);
        for (int on = 0; on < 2; ++on)
            for (int gap = 0; gap < 2; ++gap)
                original ? Orig<void>(0x00591A50, meta, on, gap) : meta->Highlight(on, gap);
        PushArena(t);

        // Constructors over dirty memory; room for more (growing, shrinking, from none); the destructors.
        std::memset(Ptr<void>(kMeta), 0xCD, 0x40);
        void* r = original ? Orig<void*>(0x00590B60, Ptr<CConcreteMetaPiece>(kMeta))
                           : Ptr<CConcreteMetaPiece>(kMeta)->Construct();
        t.push_back(Rel(reinterpret_cast<uint32_t>(r)));
        PushArena(t);
        std::memset(Ptr<void>(kMeta), 0xCD, 0x40);
        r = original ? Orig<void*>(0x00590FA0, Ptr<CAbstractMetaPiece>(kMeta)) : Ptr<CAbstractMetaPiece>(kMeta)->Construct();
        t.push_back(Rel(reinterpret_cast<uint32_t>(r)));
        PushArena(t);
        const int sizes[][3] = {{5, 4, 7}, {5, 6, 3}, {0, 2, 6}, {3, 3, 2}, {5, 4, 4}, {5, 3, 9}, {0, 0, 1}, {5, 9, -1}};
        for (const auto& size : sizes) {
            meta = MakeConcrete();
            if (!size[0])
                meta->mp_extra_desc = nullptr;
            meta->m_max_entries = static_cast<uint32_t>(size[1]);
            original ? Orig<void>(0x00590AD0, meta, size[2]) : meta->Initialize(size[2]);
            PushArena(t);
        }
        for (unsigned int flags : {0u, 1u, 2u}) {
            for (bool extra : {true, false}) {
                meta = MakeConcrete();
                if (!extra)
                    meta->mp_extra_desc = nullptr;
                r = original ? Orig<void*>(0x00591670, meta, flags) : meta->Destroy(flags);
                t.push_back(Rel(reinterpret_cast<uint32_t>(r)));
                meta = MakeConcrete();
                if (!extra)
                    meta->mp_extra_desc = nullptr;
                r = original ? Orig<void*>(0x00591BB0, meta, flags) : static_cast<CConcreteMetaPiece*>(meta)->Destroy(flags);
                t.push_back(Rel(reinterpret_cast<uint32_t>(r)));
                meta = MakeConcrete();
                if (!extra)
                    meta->mp_extra_desc = nullptr;
                original ? Orig<void>(0x005917E0, meta) : meta->DestroyBody();
                PushArena(t);
            }
        }
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "park metapieces: names, descriptors, areas, shown, room, destructors");
}

void TestParkMetaPieces2()
{
    SaveGlobals({{0x007CE604, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUp(run);
        // Pieces added (their positions in the metapiece), the area from them, the bounds, each one's place.
        CMetaPiece* meta = MakeMeta(kMeta, CConcreteMetaPiece::kVtable, CMetaPiece::kConcrete, 0, 0);
        meta->mp_extra_desc = Ptr<SMetaDescriptor>(kExtra);
        meta->m_max_entries = 7;
        meta->m_cell_area = {9, -6, 9, 0, 0, 0};
        for (int i = 0; i < 6; ++i) {
            MakeClone(kPieces + i * 0x30, kSources + (i % 4) * 0x30, 0x7000 + i * 0x10, i ^ 1, 0.0f, 0.0f, 0.0f);
            Ptr<CPiece>(kPieces + i * 0x30)->m_pos.w = 3.0f;
            GridDims pos = {static_cast<uint8_t>(i * 2), static_cast<int8_t>(i == 5 ? -1 : i == 2 ? 1 : i), static_cast<uint8_t>(5 - i),
                            9, 9, 9};
            *Ptr<GridDims>(kOut) = pos;
            uint32_t flag = i == 4 ? 0x100u : static_cast<uint32_t>(i & 1);
            original ? Orig<void>(0x00590ED0, meta, Ptr<CPiece>(kPieces + i * 0x30), Ptr<GridDims>(kOut), i * 5, flag)
                     : static_cast<CConcreteMetaPiece*>(meta)->AddPiece(Ptr<CPiece>(kPieces + i * 0x30), Ptr<GridDims>(kOut),
                                                                       i * 5, flag != 0);
        }
        // (isReal from the flag's low byte in the original: 0x100 reads 0 there; ours takes a bool - set as it would be)
        meta->mp_extra_desc[2].isReal = false;
        PushArena(t);
        original ? Orig<void>(0x00591AB0, meta) : meta->FinishPieces();
        PushArena(t);
        original ? Orig<void>(0x00590B90, meta, Ptr<Mth::Vector>(kOut), Ptr<Mth::Vector>(kOut + 0x10))
                 : meta->GetBounds(Ptr<Mth::Vector>(kOut), Ptr<Mth::Vector>(kOut + 0x10));
        PushArena(t);
        for (uint32_t i = 0; i < 6; ++i) {
            Mth::Vector* v = original ? Orig<Mth::Vector*>(0x005909C0, meta, Ptr<Mth::Vector>(kOut), i)
                                      : meta->GetRelativePosition(Ptr<Mth::Vector>(kOut), i);
            t.push_back(Rel(reinterpret_cast<uint32_t>(v)));
            Ptr<Mth::Vector>(kOut)->w = 0.0f; // (a stack leftover in the original)
            PushArena(t);
        }
        // Placed in the world, turned.
        for (float angle : {0.0f, 90.0f, -45.5f}) {
            Mth::Vector at = {1000.0f + angle, -48.0f, 333.3f, 0.5f};
            original ? Orig<void>(0x00591800, meta, at.x, at.y, at.z, at.w, angle) : meta->SetPosition(at, angle);
            PushArena(t);
        }
        // Precisely in an area or not.
        const GridDims areas[] = {{13, -5, 10, 1, 1, 2}, {13, -6, 12, 1, 2, 1}, {9, -6, 9, 2, 1, 2},  {0, 0, 0, 255, 127, 255}, {20, 3, 9, 2, 1, 2}, {9, -9, 9, 2, 5, 2},
                                  {13, 4, 5, 1, 1, 1}, {10, 3, 13, 1, 1, 1},     {9, 3, 14, 1, 1, 1}, {19, 6, 9, 5, 5, 9}};
        for (const GridDims& area : areas) {
            for (uint32_t precise : {0u, 1u, 0x100u}) {
                uint32_t words[2];
                std::memcpy(words, &area, 6);
                words[1] &= 0xFFFF;
                t.push_back(original ? Orig<bool>(0x00590CE0, meta, words[0], words[1], precise)
                                     : meta->Intersects(area, precise));
            }
        }
        // Areas just touching its own (its edges).
        {
            const GridDims& c = meta->m_cell_area;
            const GridDims edges[] = {{static_cast<uint8_t>(c.x + c.w), c.y, c.z, 3, 3, 3},
                                      {static_cast<uint8_t>(c.x - 3), c.y, c.z, 3, 3, 3},
                                      {c.x, static_cast<int8_t>(c.y + c.h), c.z, 3, 3, 3},
                                      {c.x, static_cast<int8_t>(c.y - 3), c.z, 3, 3, 3},
                                      {c.x, c.y, static_cast<uint8_t>(c.z + c.l), 3, 3, 3},
                                      {c.x, c.y, static_cast<uint8_t>(c.z - 3), 3, 3, 3}};
            for (const GridDims& area : edges)
                t.push_back(original ? Orig<bool>(0x00590CE0, meta, *reinterpret_cast<const uint32_t*>(&area),
                                                  static_cast<uint32_t>(area.h | (area.l << 8)), 0u)
                                     : meta->Intersects(area, 0));
        }
        for (int i = 0; i < 6; ++i) // (none flagged: every piece counts)
            (i < 2 ? meta->m_desc[i] : meta->mp_extra_desc[i - 2]).isReal = false;
        for (const GridDims& area : areas)
            t.push_back(original ? Orig<bool>(0x00590CE0, meta, *reinterpret_cast<const uint32_t*>(&area),
                                              static_cast<uint32_t>(area.h | (area.l << 8)), 1u)
                                 : meta->Intersects(area, 1));
        PushArena(t);
        // A flat one (its pieces no height): at least a step high.
        CMetaPiece* flat = MakeMeta(kMeta + 0x40, CConcreteMetaPiece::kVtable, CMetaPiece::kConcrete, 2, 0);
        for (int i = 0; i < 2; ++i) {
            MakeClone(kPieces + 0x200 + i * 0x30, kSources + 0x90, 0x7100, 0, 150.5f * i, 0.0f, -80.25f * i);
            SetEntry(flat, i, 0, 0, 0, 0, false, At(kPieces + 0x200 + i * 0x30));
        }
        original ? Orig<void>(0x00591AB0, flat) : flat->FinishPieces();
        PushArena(t);
        // Turned in its cells, by every amount (a concrete metapiece).
        for (int rot : {1, 3, 0, 2, 2, 7, -1, 6, 5}) {
            original ? Orig<void>(0x005907B0, meta, rot) : meta->SetRot(rot);
            PushArena(t);
        }
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "park metapieces: pieces added, bounds, places, positions, areas, turns");
}

void TestParkMetaPieces3()
{
    SaveGlobals({{0x007CE604, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUp(run);
        // The palette: two single pieces (flag 0x80), two of several pieces.
        g_palette.clear();
        for (uint32_t i = 0; i < 4; ++i) {
            uint32_t at = kPalette + i * 0x80;
            CMetaPiece* entry = MakeMeta(at, CAbstractMetaPiece::kVtable, CMetaPiece::kAbstract | (i < 2 ? 0x80u : 0u),
                                         i < 2 ? 1 : 3 + i);
            entry->m_cell_area = {0, 0, 0, static_cast<uint8_t>(2 + i), static_cast<uint8_t>(1 + i), static_cast<uint8_t>(3 - i / 2)};
            for (uint32_t j = 0; j < entry->m_num_used_entries; ++j)
                SetEntry(entry, static_cast<int>(j), static_cast<uint8_t>(j), static_cast<int8_t>(j % 2 ? -1 : 2),
                         static_cast<uint8_t>(j * 2 + i), static_cast<uint8_t>((j + i) % 4), j == 1, 0x5000 + j);
            entry->m_name_checksum = 0x9000 + i;
            g_palette[0x9000 + i] = At(at);
        }
        // An abstract metapiece of seven of those: the nested ones turned every way (6: none of the four).
        CMetaPiece* meta = MakeMeta(kMeta, CAbstractMetaPiece::kVtable, CMetaPiece::kAbstract, 7, kExtra);
        meta->m_cell_area = {0, 0, 0, 11, 4, 9};
        const uint32_t names[7] = {0x9002, 0x9003, 0x9000, 0x9002, 0x9003, 0x9002, 0x9001};
        const uint8_t rots[7] = {0, 1, 1, 2, 3, 6, 0};
        for (int i = 0; i < 7; ++i)
            SetEntry(meta, i, static_cast<uint8_t>(i + 1), static_cast<int8_t>(-i), static_cast<uint8_t>(2 * i), rots[i],
                     (i & 1) != 0, names[i]);
        for (int index = -1; index < 32; ++index) {
            std::memset(Ptr<void>(kOut), 0xAB, 12);
            void* r = original ? Orig<void*>(0x005911D0, meta, Ptr<SMetaDescriptor>(kOut), index)
                               : static_cast<CAbstractMetaPiece*>(meta)->GetEntry(Ptr<SMetaDescriptor>(kOut), index);
            t.push_back(Rel(reinterpret_cast<uint32_t>(r)));
            if (index >= 0 && index < 29) // (past the end the original returns a stack leftover)
                for (int k = 0; k < 3; ++k)
                    t.push_back(Ptr<uint32_t>(kOut)[k]);
        }
        std::memset(Ptr<void>(kOut), 0, 12);
        t.push_back(static_cast<uint32_t>(original ? Orig<int>(0x00590400, meta) : meta->CountPieces()));
        for (int i = -1; i < 7; ++i) {
            void* p = original ? Orig<void*>(0x00590FD0, meta, static_cast<uint32_t>(i))
                               : static_cast<CAbstractMetaPiece*>(meta)->GetPiece(static_cast<uint32_t>(i));
            t.push_back(Rel(reinterpret_cast<uint32_t>(p)));
        }
        // Turned in its cells (an abstract metapiece: the palette's sizes).
        for (int rot : {1, 3, 0, 2, 2, 5, 8}) {
            original ? Orig<void>(0x005907B0, meta, rot) : meta->SetRot(rot);
            PushArena(t);
        }
        // Pieces and palette metapieces added (a cloned piece isn't: only counted), growing the area.
        meta = MakeMeta(kMeta, CAbstractMetaPiece::kVtable, CMetaPiece::kAbstract, 0, 0);
        meta->mp_extra_desc = Ptr<SMetaDescriptor>(kExtra);
        meta->m_max_entries = 9;
        meta->m_cell_area = {4, 4, 4, 1, 0, 2};
        MakeClone(kPieces, kSources, 0x7000, 1, 0.0f, 0.0f, 0.0f);
        for (int i = 0; i < 8; ++i) {
            GridDims pos = {static_cast<uint8_t>(i), static_cast<int8_t>(i == 3 ? -2 : i - 1), static_cast<uint8_t>(7 - i),
                            static_cast<uint8_t>(1 + i % 3), static_cast<uint8_t>(i <= 1 ? 0 : 1), static_cast<uint8_t>(i % 2 + 1)};
            *Ptr<GridDims>(kOut) = pos;
            uint32_t flag = i == 5 ? 0x7u : static_cast<uint32_t>(i & 1);
            if (i % 3 == 0) {
                CMetaPiece* entry = i == 6 ? nullptr : Ptr<CMetaPiece>(kPalette + 0x80 * (i % 4));
                original ? Orig<void>(0x00591100, meta, entry, Ptr<GridDims>(kOut), i, flag)
                         : static_cast<CAbstractMetaPiece*>(meta)->AddMetaPiece(entry, Ptr<GridDims>(kOut), i,
                                                                               static_cast<uint8_t>(flag));
            } else {
                CPiece* piece = Ptr<CPiece>(i == 4 ? kPieces : kSources + (i % 4) * 0x30);
                original ? Orig<void>(0x00591020, meta, piece, Ptr<GridDims>(kOut), i, flag)
                         : static_cast<CAbstractMetaPiece*>(meta)->AddPiece(piece, Ptr<GridDims>(kOut), i,
                                                                           static_cast<uint8_t>(flag));
            }
            PushArena(t);
        }
        // The pieces as a script array (an abstract metapiece: its names).
        CMetaPiece* concrete = MakeConcrete();
        original ? Orig<void>(0x005916B0, concrete, Ptr<Script::CArray>(kArray)) : concrete->GetPieceList(Ptr<Script::CArray>(kArray));
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "park metapieces: the palette's entries, counts, sources, turns, pieces added");
}

} // namespace

SELFTEST(Early, TestParkMetaPieces1, ViewerTestKit::Cleanup);
SELFTEST(Early, TestParkMetaPieces2, ViewerTestKit::Cleanup);
SELFTEST(Early, TestParkMetaPieces3, ViewerTestKit::Cleanup);
