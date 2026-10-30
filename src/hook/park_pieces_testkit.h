#pragma once

// Shared pieces of the self-tests for the park editor's pieces, metapieces and generator (selftests_park_pieces.cpp,
// selftests_park_metapieces.cpp, selftests_park_generator.cpp): recording stand-ins for the engine's sectors,
// geometry and scenes, the rotation matrix and D3DX's multiply, on viewer_testkit.h's arena.

#include <cstdint>
#include <cstring>

#include "game/ed/park.h"
#include "game/gfx/engine.h"
#include "game/gfx/sector.h"
#include "game/mth/matrix.h"
#include "hook/viewer_testkit.h"

namespace ParkPiecesTestKit {

using namespace ViewerTestKit;

constexpr uint32_t kSector = 0x3000;   // the sectors GetSector answers (0x40 of them, by name)
constexpr uint32_t kGeometry = 0x3100; // a geometry (its vtable at kGeometryVtable)
constexpr uint32_t kGeometryVtable = 0x3200;

inline uint32_t g_geometry; // what GetGeometry answers
inline void* g_setColor;

inline void RecWords(uint32_t tag, uint32_t at, int count)
{
    g_trace.push_back(tag);
    for (int i = 0; i < count; ++i)
        g_trace.push_back(reinterpret_cast<const uint32_t*>(at)[i]);
}

// The sectors (name 0xDEAD has none), their geometry and the scenes' sector calls.
inline void InstallScene(StandIns& s)
{
    InstallCommon(s);
    s.At({0x0048C510, AddressOf(&Nx::Engine::GetSector)}, 0, [](Regs& r) {
        Rec(0xC510, {r.args[0]});
        if (r.args[0] == 0xDEAD)
            return 0u;
        // (0x20 apart: +0x14 the sector's geometry - what GetGeometry (0x004A2BA0, a read of that word) answers)
        uint32_t sector = At(kSector) + (r.args[0] & 0x30) * 2;
        *reinterpret_cast<uint32_t*>(sector + 0x14) = g_geometry;
        return sector;
    });
    s.At({0x0049E760, AddressOf(&Nx::CSector::SetYRotation)}, 4, [](Regs& r) {
        Rec(0xE760, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x0049E9B0, AddressOf(&Nx::CSector::SetActive)}, 4, [](Regs& r) {
        Rec(0xE9B0, {r.ecx, r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x0049E680, AddressOf(&Nx::CSector::SetVisibility)}, 4, [](Regs& r) {
        Rec(0xE680, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x0049E6C0, AddressOf(&Nx::CSector::SetWorldPosition)}, 4, [](Regs& r) {
        Rec(0xE6C0, {r.ecx});
        RecWords(0xE6C1, r.args[0], 4);
        return 0u;
    });
    s.At({0x0049E7C0, AddressOf(&Nx::CSector::SetScale)}, 4, [](Regs& r) {
        Rec(0xE7C0, {r.ecx});
        RecWords(0xE7C1, r.args[0], 3); // (w is a stack leftover in the original)
        return 0u;
    });
    // (a sector's geometry, +0x14, is read as it is: GetSector sets it)
    s.At({0x0048DEE0}, 4, [](Regs& r) {
        Rec(0xDEE0, {r.ecx});
        RecWords(0xDEE1, r.args[0], 16);
        return 0u;
    });
    s.At({0x0048D060}, 0, [](Regs& r) {
        Rec(0xD060, {r.ecx});
        return 0u;
    });
    g_setColor = s.Thunk(4, [](Regs& r) {
        Rec(0x0C01, {r.ecx, r.args[0]});
        return 0u;
    });
    // The rotation: a matrix made from the angle's bits (so a wrong angle shows).
    s.At({0x00404E20, AddressOf(static_cast<Mth::Matrix& (*)(Mth::Matrix&, float)>(&Mth::CreateRotateYMatrix))}, 0,
         [](Regs& r) {
             Rec(0x4E20, {r.args[1]});
             auto* m = reinterpret_cast<float*>(r.args[0]);
             for (int i = 0; i < 16; ++i)
                 m[i] = static_cast<float>(static_cast<int>((r.args[1] >> (i % 8)) & 0xFF) - 100 + i) * 0.125f;
             return r.args[0];
         });
    // D3DXMatrixMultiply (stdcall): a plain product.
    s.At({0x005BFA8C}, 12, [](Regs& r) {
        float a[16], b[16], out[16];
        std::memcpy(a, reinterpret_cast<void*>(r.args[1]), 64);
        std::memcpy(b, reinterpret_cast<void*>(r.args[2]), 64);
        RecWords(0xFA8D, r.args[1], 16);
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                out[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] + a[i * 4 + 3] * b[12 + j];
        std::memcpy(reinterpret_cast<void*>(r.args[0]), out, 64);
        return r.args[0];
    });
}

// The geometry's vtable (its SetColor recorded); call after SetUpArena.
inline void SetUpGeometry()
{
    reinterpret_cast<void**>(At(kGeometryVtable))[6] = g_setColor;
    // (slots 8 and 20, ClearColor and SetOrientation, as the stood-in dispatchers' addresses)
    reinterpret_cast<void**>(At(kGeometryVtable))[8] = reinterpret_cast<void*>(0x0048D060);
    reinterpret_cast<void**>(At(kGeometryVtable))[20] = reinterpret_cast<void*>(0x0048DEE0);
    *Ptr<uint32_t>(kGeometry) = At(kGeometryVtable);
}

// A source piece at `at` (name, size).
inline void MakeSource(uint32_t at, uint32_t name, float x, float y, float z)
{
    auto* source = Ptr<uint32_t>(at);
    std::memset(source, 0, 0x30);
    source[0] = Ed::CSourcePiece::kVtable;
    source[1] = name;
    source[2] = Ed::CPiece::kSource;
    float dims[4] = {x, y, z, 0.75f};
    std::memcpy(source + 4, dims, 16);
}
// A cloned piece at `at` of the source at `source` (sector, turn, position).
inline void MakeClone(uint32_t at, uint32_t source, uint32_t sector, int rot, float x, float y, float z)
{
    auto* clone = Ptr<uint32_t>(at);
    std::memset(clone, 0, 0x2C);
    clone[0] = Ed::CClonedPiece::kVtable;
    clone[1] = sector;
    clone[2] = Ed::CPiece::kCloned;
    clone[4] = 7;
    clone[5] = At(source);
    float pos[4] = {x, y, z, 1.0f};
    std::memcpy(clone + 6, pos, 16);
    clone[10] = static_cast<uint32_t>(rot);
}

template <typename R, typename... A>
R Orig(uint32_t address, A... args)
{
    return Original<R(__thiscall*)(A...)>(address)(args...);
}

} // namespace ParkPiecesTestKit
