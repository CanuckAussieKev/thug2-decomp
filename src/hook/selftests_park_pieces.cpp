// Self-tests for the park editor's pieces and cell boxes (src/game/ed/park_pieces.cpp): the original and ours on the
// same stand-in pieces (viewer_testkit.h's arena), the engine's sectors, geometry, matrices and debug output stood in
// for by recording code; the calls, results and the whole arena compared.

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/ed/park.h"
#include "hook/park_pieces_testkit.h"

namespace {

using namespace ParkPiecesTestKit;
using Ed::CClonedPiece;
using Ed::CPiece;
using Ed::CSourcePiece;
using Ed::GridDims;

constexpr uint32_t kSource = 0x1000;
constexpr uint32_t kClone = 0x1100;
constexpr uint32_t kOut = 0x1200;
constexpr uint32_t kDims = 0x1300;

// A source piece and a cloned piece of it, with sizes that land on and next to cell edges.
void MakePieces(float x, float y, float z)
{
    MakeSource(kSource, 0x1111, x, y, z);
    MakeClone(kClone, kSource, 0x2222, 0, 1.5f, 0.0f, 0.0f);
}

void TestParkPieces1()
{
    SaveGlobals({});
    StandIns* s = new StandIns;
    InstallScene(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUpArena(run);
        g_printfArgs = 6;
        // GridDims
        auto* d = Ptr<GridDims>(kDims);
        GridDims* r = original ? Orig<GridDims*>(0x005901B0, d, 3, -5, 250, 4, 9, 200) : d->Set(3, -5, 250, 4, 9, 200);
        t.push_back(Rel(reinterpret_cast<uint32_t>(r)));
        for (int i : {0, 4, 5, -2, 100}) {
            uint8_t* b = original ? Orig<uint8_t*>(0x005901E0, d, i) : d->Byte(i);
            t.push_back(Rel(reinterpret_cast<uint32_t>(b)));
        }
        original ? Orig<void>(0x00590210, d) : d->PrintContents();
        for (int y : {-5, 0, 100, -128}) {
            d->y = static_cast<int8_t>(y);
            original ? Orig<void>(0x005901F0, d) : d->MakeInfiniteOnY();
            t.push_back(d->h);
        }
        original ? Orig<void>(0x00590200, d) : d->MakeInfinitelyHigh();
        PushArena(t);

        // Constructors (over dirty memory) and the destructors.
        std::memset(Ptr<void>(kSource), 0xCD, 0x30);
        std::memset(Ptr<void>(kClone), 0xCD, 0x2C);
        void* p = original ? Orig<void*>(0x005905C0, Ptr<CSourcePiece>(kSource)) : Ptr<CSourcePiece>(kSource)->Construct();
        t.push_back(Rel(reinterpret_cast<uint32_t>(p)));
        p = original ? Orig<void*>(0x00590620, Ptr<CClonedPiece>(kClone)) : Ptr<CClonedPiece>(kClone)->Construct();
        t.push_back(Rel(reinterpret_cast<uint32_t>(p)));
        PushArena(t);
        for (unsigned int flags : {0u, 1u, 2u, 3u}) {
            p = original ? Orig<void*>(0x00590490, Ptr<CPiece>(kClone), flags) : Ptr<CPiece>(kClone)->Destroy(flags);
            t.push_back(Rel(reinterpret_cast<uint32_t>(p)));
            p = original ? Orig<void*>(0x00590640, Ptr<CSourcePiece>(kSource), flags)
                         : Ptr<CSourcePiece>(kSource)->Destroy(flags);
            t.push_back(Rel(reinterpret_cast<uint32_t>(p)));
            PushArena(t);
            *Ptr<uint32_t>(kClone) = 0x12345;
            *Ptr<uint32_t>(kSource) = 0x23456;
        }
        original ? Orig<void>(0x00590240, Ptr<CPiece>(kClone)) : Ptr<CPiece>(kClone)->DestroyBody();
        PushArena(t);

        // Sizes: of a source and of a clone at each turn; in cells.
        const float sizes[][3] = {{238.0f, 48.0f, 120.0f}, {117.9f, 46.0f, 359.5f}, {0.0f, -3.0f, 1e9f}, {600.0f, 95.99f, 1.0f}};
        for (const auto& size : sizes) {
            MakePieces(size[0], size[1], size[2]);
            auto* source = Ptr<CPiece>(kSource);
            auto* clone = Ptr<CPiece>(kClone);
            for (int rot = 0; rot < 5; ++rot) {
                clone->m_rot = rot;
                for (CPiece* piece : {source, clone}) {
                    auto* out = Ptr<Mth::Vector>(kOut);
                    *out = {9.0f, 9.0f, 9.0f, 9.0f};
                    Mth::Vector* v = original ? Orig<Mth::Vector*>(0x00590530, piece, out, rot ^ 1)
                                              : piece->GetDimsTurned(out, rot ^ 1);
                    t.push_back(Rel(reinterpret_cast<uint32_t>(v)));
                    if ((rot ^ 1) & 1)
                        t.push_back(Bits(out->w)); // (only defined for odd turns)
                    out->w = 0.0f;
                    *Ptr<uint32_t>(kDims) = 0x77777777;
                    Ptr<uint32_t>(kDims)[1] = 0x77777777;
                    original ? Orig<void>(0x005904C0, piece, d) : piece->GetCellDims(d);
                    PushArena(t);
                }
                auto* out = Ptr<Mth::Vector>(kOut);
                Mth::Vector* v = original ? Orig<Mth::Vector*>(0x00590710, clone, out)
                                          : static_cast<CClonedPiece*>(clone)->GetDims(out);
                t.push_back(Rel(reinterpret_cast<uint32_t>(v)));
                out->w = 0.0f; // (a stack leftover in the original)
                v = original ? Orig<Mth::Vector*>(0x00590600, Ptr<CSourcePiece>(kSource), Ptr<Mth::Vector>(kOut + 0x10))
                             : Ptr<CSourcePiece>(kSource)->GetDims(Ptr<Mth::Vector>(kOut + 0x10));
                t.push_back(Rel(reinterpret_cast<uint32_t>(v)));
                PushArena(t);
            }
        }
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "park pieces: cell boxes, constructors, destructors, sizes");
}

void TestParkPieces2()
{
    SaveGlobals({});
    StandIns* s = new StandIns;
    InstallScene(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUpArena(run);
        SetUpGeometry();
        MakePieces(240.0f, 48.0f, 120.0f);
        auto* clone = Ptr<CPiece>(kClone);
        t.push_back(static_cast<uint32_t>(original ? Orig<int>(0x00590250, clone) : clone->GetId()));
        for (int update : {0, 1, 2, -1}) {
            original ? Orig<void>(0x00590260, clone, 3 + update, update) : clone->SetRot(3 + update, update);
            auto* position = Ptr<Mth::Vector>(kOut);
            *position = {1.5f + update, -2.25f, 1000.0f, 0.5f * update};
            original ? Orig<void>(0x00590670, clone, position, update) : clone->SetPosition(position, update);
            PushArena(t);
        }
        for (uint32_t sector : {0x2222u, 0x1230u}) {
            clone->m_sector = sector;
            for (bool on : {true, false}) {
                original ? Orig<void>(0x00590290, clone, on) : clone->SetActive(on);
                original ? Orig<void>(0x005902B0, clone, on) : clone->SetVisible(on);
            }
        }
        for (int rot : {0, 1, 2, 3, -1}) {
            clone->m_rot = rot;
            float length = 2.5f + rot;
            original ? Orig<void>(0x005906B0, clone, length) : clone->SetLength(length);
        }
        // Turned: with geometry and without; tinted: no sector, no geometry, on (gap or not), off.
        for (uint32_t geometry : {At(kGeometry), 0u}) {
            g_geometry = geometry;
            for (float degrees : {0.0f, 90.0f, 37.5f, -270.0f}) {
                original ? Orig<void>(0x00591510, clone, degrees) : clone->SetAngle(degrees);
            }
            for (uint32_t sector : {0x2222u, 0xDEADu}) {
                clone->m_sector = sector;
                for (int on = 0; on < 2; ++on)
                    for (int gap = 0; gap < 2; ++gap)
                        original ? Orig<void>(0x00591600, clone, on != 0, gap != 0) : clone->SetHighlight(on != 0, gap != 0);
            }
        }
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "park pieces: turned, moved, shown, stretched, tinted");
}

} // namespace

SELFTEST(Early, TestParkPieces1, ViewerTestKit::Cleanup);
SELFTEST(Early, TestParkPieces2, ViewerTestKit::Cleanup);
