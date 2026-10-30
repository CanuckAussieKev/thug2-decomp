// Self-tests for the script commands 0x005B2420-0x005B34A0 (face textures, changing textures, HSV / RGB, the nearest
// rail point, the park's name, the special bar, the initials, the fireball level, last names, the light's debug
// lines, the slap game): each original against ours on identical stand-in state; the textures, the texture
// dictionary, the rail editor, the park generator's search, the records, the game mode and the skaters are stood in
// for by recording code (the game's functions and ours alike). Structure getters run for real.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/skate/skate_module.h"
#include "game/ed/park_editor.h"
#include "game/gfx/face_texture_cfuncs.h"
#include "game/script/game_util_cfuncs.h"
#include "game/skate/records.h"
#include "game/skater/skater.h"
#include "game/ed/rail_editor.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/texture_dict.h"
#include "game/skate/game_mode.h"
#include "game/skate/score.h"
#include "hook/gs4_testkit.h"

namespace {

using namespace Gs4Tests;

alignas(16) uint8_t g_tSkate[0x100];
alignas(16) uint8_t g_tProfile[0x64];
alignas(16) uint8_t g_tFace[0x4480];
alignas(16) uint8_t g_tTextures[2][0x40];
alignas(16) uint8_t g_tPixels[0x300];
alignas(16) uint8_t g_tRailEditor[0x20];
alignas(16) uint8_t g_tRecords[0x10];
alignas(16) uint8_t g_tGameMode[0x10];
alignas(16) uint8_t g_tSkaters[2][0x200];
char g_tInitials[8] = "XYZ";
uint32_t g_tLoadResult;
uint8_t g_tBitsPerPixel;
int g_tPaletteSize;
int g_tPixelSize;
float g_tRailDistance;
float g_tGeometryDistance;
uint32_t g_tModeName;
int g_tFireball;

uint32_t TextureId(uint32_t address)
{
    for (uint32_t i = 0; i < 2; ++i)
        if (address == reinterpret_cast<uint32_t>(g_tTextures[i]))
            return 0x7E00 + i;
    return address;
}

void StandInTextureCalls(StandIns& s)
{
    s.At({0x00564F80, AddressOf(static_cast<Obj::CSkaterProfile* (Mdl::Skate::*)(Script::CStruct*)>(&Mdl::Skate::GetProfile))}, 4, [](Regs& r) {
        Record({0x4F80, r.ecx == reinterpret_cast<uint32_t>(g_tSkate), r.args[0]});
        return reinterpret_cast<uint32_t>(g_tProfile);
    });
    s.At({0x0049F540, AddressOf(&Nx::CTexDict::GetTexture)}, 4, [](Regs& r) {
        Record({0xF540, r.ecx, r.args[0]});
        return r.args[0] == 0x7E7E0001 ? reinterpret_cast<uint32_t>(g_tTextures[0])
               : r.args[0] == 0x7E7E0002 ? reinterpret_cast<uint32_t>(g_tTextures[1])
                                         : 0u;
    });
    s.At({0x0049FA20}, 0x1C, [](Regs& r) {
        Record({0xFA20, r.ecx, r.args[0] - reinterpret_cast<uint32_t>(g_tFace), r.args[1], r.args[2], r.args[3] & 0xFF,
                r.args[4] & 0xFF, r.args[5] & 0xFF, r.args[6] & 0xFF});
        return g_tLoadResult;
    });
    s.At({0x0048DF50}, 4, [](Regs& r) {
        Record({0xDF50, TextureId(r.ecx)});
        auto* header = reinterpret_cast<uint8_t*>(r.args[0]);
        for (int i = 0; i < 0x20; ++i)
            header[i] = static_cast<uint8_t>(0xA0 + i);
        return 0u;
    });
    s.At({0x0048DF30}, 0, [](Regs& r) {
        Record({0xDF30, TextureId(r.ecx)});
        return static_cast<uint32_t>(g_tPaletteSize);
    });
    s.At({0x0049F3E0}, 0, [](Regs& r) {
        Record({0xF3E0, TextureId(r.ecx)});
        return 0xABCDEF00u | g_tBitsPerPixel; // (only AL is the result)
    });
    s.At({0x0048DF40}, 8, [](Regs& r) {
        Record({0xDF40, TextureId(r.ecx), r.args[1]});
        auto* palette = reinterpret_cast<uint8_t*>(r.args[0]);
        for (uint32_t i = 0; i < r.args[1] && i < 0x400; ++i)
            palette[i] = static_cast<uint8_t>(i * 7 + 1);
        return 0u;
    });
    s.At({0x0048DF00}, 0, [](Regs& r) {
        Record({0xDF00, TextureId(r.ecx)});
        return reinterpret_cast<uint32_t>(g_tPixels);
    });
    s.At({0x0049F3A0}, 0, [](Regs& r) {
        Record({0xF3A0, TextureId(r.ecx)});
        return static_cast<uint32_t>(g_tPixelSize);
    });
    s.At({0x00487420, AddressOf(&Gfx::CFaceTexture::SetImage)}, 8, [](Regs& r) {
        Record({0x7420, r.ecx == reinterpret_cast<uint32_t>(g_tFace), r.args[1],
                HashBytes(reinterpret_cast<const uint8_t*>(r.args[0]), 0x4420)});
        return r.args[0] + 0x4420;
    });
    s.At({0x0048DEC0}, 16, [](Regs& r) {
        Record({0xDEC0, TextureId(r.ecx), r.args[0], r.args[1], r.args[2] & 0xFF, r.args[3]});
        return 0u;
    });
    s.At({0x0049F360}, 12, [](Regs& r) {
        Record({0xF360, TextureId(r.ecx), r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x0048DEF0}, 20, [](Regs& r) {
        Record({0xDEF0, TextureId(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3] & 0xFF, r.args[4]});
        return 0u;
    });
    s.At({0x0048DE50}, 8, [](Regs& r) {
        Record({0xDE50, TextureId(r.ecx), r.args[0], r.args[1] & 0xFF});
        return 0u;
    });
    // the rail point search, the generator's search
    s.At({0x004F4970, AddressOf(&Ed::CRailEditorComponent::Instance)}, 0, [](Regs&) {
        Record({0x4970});
        return reinterpret_cast<uint32_t>(g_tRailEditor);
    });
    s.At({0x004F4710, AddressOf(&Ed::CRailEditorComponent::FindNearestPoint)}, 0x1C, [](Regs& r) {
        auto* pos = reinterpret_cast<float*>(r.args[0]);
        Record({0x4710, r.ecx == reinterpret_cast<uint32_t>(g_tRailEditor), Bits(pos[0]), Bits(pos[1]), Bits(pos[2]),
                Bits(*reinterpret_cast<float*>(r.args[2])), *reinterpret_cast<uint32_t*>(r.args[3]),
                *reinterpret_cast<uint32_t*>(r.args[4]), r.args[5], r.args[6]});
        auto* point = reinterpret_cast<float*>(r.args[1]);
        point[0] = 1.25f;
        point[1] = -2.5f;
        point[2] = 3.75f;
        *reinterpret_cast<float*>(r.args[2]) = g_tRailDistance;
        *reinterpret_cast<uint32_t*>(r.args[3]) = 0x4A110001;
        *reinterpret_cast<int*>(r.args[4]) = 6;
        return 1u;
    });
    s.At({0x0058DF80, AddressOf(&Ed::CParkEditor::FindRestart)}, 0, [](Regs& r) {
        auto* pos = reinterpret_cast<float*>(r.args[0]);
        Record({0xDF80, Bits(pos[0]), Bits(*reinterpret_cast<float*>(r.args[2]))});
        auto* point = reinterpret_cast<float*>(r.args[1]);
        point[0] = 10.5f;
        point[1] = 20.5f;
        point[2] = -30.5f;
        *reinterpret_cast<float*>(r.args[2]) = g_tGeometryDistance;
        return 1u;
    });
    // skaters, records, the game mode
    s.At({0x005665C0, AddressOf(&Mdl::Skate::GetSkater)}, 4, [](Regs& r) {
        Record({0x65C0, r.ecx == reinterpret_cast<uint32_t>(g_tSkate), r.args[0]});
        return reinterpret_cast<uint32_t>(g_tSkaters[r.args[0] & 1]);
    });
    s.At({0x00566520, AddressOf(&Mdl::Skate::GetLocalSkater)}, 0, [](Regs& r) {
        Record({0x6520, r.ecx == reinterpret_cast<uint32_t>(g_tSkate)});
        return reinterpret_cast<uint32_t>(g_tSkaters[1]);
    });
    s.At({0x00580480, AddressOf(&Obj::CSkater::GetScore)}, 0, [](Regs& r) {
        Record({0x0480, r.ecx - reinterpret_cast<uint32_t>(g_tSkaters)});
        return r.ecx + 0x40;
    });
    s.At({0x005612D0, AddressOf(&Mdl::Score::SetSpecialBarColors)}, 0, [](Regs& r) {
        Record({0x12D0, r.ecx - reinterpret_cast<uint32_t>(g_tSkaters)});
        return 0u;
    });
    s.At({0x0057EEA0, AddressOf(&Records::CGameRecords::SetDefaultInitials)}, 4, [](Regs& r) {
        Record({0xEEA0, r.ecx == reinterpret_cast<uint32_t>(g_tRecords), Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    s.At({0x005531F0, AddressOf(&Game::CGameMode::GetFireballLevel)}, 0, [](Regs& r) {
        Record({0x31F0, r.ecx == reinterpret_cast<uint32_t>(g_tGameMode)});
        return static_cast<uint32_t>(g_tFireball);
    });
    s.At({0x005532A0, AddressOf(&Game::CGameMode::GetNameChecksum)}, 0, [](Regs& r) {
        Record({0x32A0, r.ecx == reinterpret_cast<uint32_t>(g_tGameMode)});
        return g_tModeName;
    });
}

void TestTextureCFuncs()
{
    SaveGlobals({{0x007CE478, 4, {}}, {0x007CE4BC, 4, {}}, {0x007CE470, 4, {}}});
    auto* standIns = new StandIns;
    StandInStructCalls(*standIns);
    StandInTextureCalls(*standIns);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        UseStandInArrayPool();
        std::memset(g_tSkate, 0, sizeof(g_tSkate));
        std::memset(g_tProfile, 0, sizeof(g_tProfile));
        reinterpret_cast<void**>(g_tProfile)[0x54 / 4] = g_tFace;
        for (size_t i = 0; i < sizeof(g_tFace); ++i)
            g_tFace[i] = static_cast<uint8_t>(i * 13);
        for (size_t i = 0; i < sizeof(g_tPixels); ++i)
            g_tPixels[i] = static_cast<uint8_t>(i * 5 + 3);
        reinterpret_cast<char**>(g_tRecords)[1] = g_tInitials;
        GameVariable<void*>(0x007CE478) = g_tSkate;
        GameVariable<void*>(0x007CE4BC) = g_tRecords;
        GameVariable<void*>(0x007CE470) = g_tGameMode;
        std::memset(g_tSkaters, 0, sizeof(g_tSkaters));
        reinterpret_cast<float*>(g_tSkaters[1] + 0x4C)[0] = 100.0f;
        reinterpret_cast<float*>(g_tSkaters[1] + 0x4C)[1] = 5.0f;
        reinterpret_cast<float*>(g_tSkaters[1] + 0x4C)[2] = -40.0f;
        g_own.Reset();
        auto call = [&](uint32_t address, CFunc ours, TestParams& params) { Call(original, address, ours, params); };
        using namespace CFuncs;
        TestParams none;

        // LoadFaceTextureFromProfile
        for (uint32_t result : {0x12345678u, 0u}) {
            g_tLoadResult = result;
            TestParams p; p.Checksum(0x21902065, 0xFACE0001);
            call(0x005B2420, CFunc_LoadFaceTextureFromProfile, p);
        }
        // SetFaceTextureFromVideoCapture
        int shapes[][3] = {{8, 0x400, 0x100}, {16, 0x200, 0x2F0}, {32, 0x40, 0x0}, {24, 0x3C, 0x13}};
        for (auto& shape : shapes) {
            g_tBitsPerPixel = static_cast<uint8_t>(shape[0]);
            g_tPaletteSize = shape[1];
            g_tPixelSize = shape[2];
            TestParams p; p.Checksum(0xD7C087AF, 0x7E7E0002);
            call(0x005B2480, CFunc_SetFaceTextureFromVideoCapture, p);
        }
        // OffsetTexture, PushTextureToPoint, ApplyRGBAToTexture, RotateTexture
        {
            constexpr uint32_t c0 = 0xB7A78C53, c1 = 0xDA7A68B8, c2 = 0xAA109C37, c3 = 0x3319CD8D;
            TestParams cases[7];
            cases[0].Checksum(0, 0x7E7E0001);
            cases[1].Checksum(0, 0x7E7E0001).Integer(0xD83D589E, 12).Integer(0x14975800, -7).Integer(c0, 0x11);
            cases[2].Checksum(0, 0x7E7E0002).Integer(c1, 0x122).Integer(0x485A0CDB, 9).Integer(0x7AF07905, 2);
            cases[3].Checksum(0, 0x7E7E0002).Integer(c2, 3).Integer(0x88CF948C, -4).Integer(0x14975800, 5);
            cases[4].Checksum(0, 0x7E7E0001).Integer(c3, 0xFE).Integer(0xD83D589E, -1);
            cases[5].Checksum(0, 0x7E7E0001).Integer(c0, 1).Integer(c1, 2).Integer(c2, 3).Integer(c3, 4);
            cases[6].Checksum(0, 0x7E7E0003).Integer(c0, 1);
            for (TestParams& p : cases) {
                call(0x005B2570, CFunc_OffsetTexture, p);
                call(0x005B2720, CFunc_PushTextureToPoint, p);
            }
            TestParams a[6];
            a[0].Checksum(0, 0x7E7E0001).Integer(0x93F60062, 10).Integer(0xFE2BE489, 20).Integer(0x8E411006, 30);
            a[1].Checksum(0, 0x7E7E0002).Integer(0x93F60062, 300).Integer(0xFE2BE489, 2).Integer(0x8E411006, 3)
                .Integer(0x174841BC, 40).Integer(0xC164842A, 1);
            a[2].Checksum(0, 0x7E7E0001).Integer(0x93F60062, 1).Integer(0xFE2BE489, 2).Integer(0x8E411006, 3)
                .Integer(0xC164842A, 2);
            a[3].Checksum(0, 0x7E7E0009).Integer(0x93F60062, 1);
            a[4].Checksum(0, 0x7E7E0002).Integer(0x93F60062, 1).Integer(0xFE2BE489, 2).Integer(0x8E411006, 3)
                .Integer(0xC164842A, 0);
            a[5].Checksum(0, 0x7E7E0001).Integer(0x93F60062, 7).Integer(0xFE2BE489, 8).Integer(0x8E411006, 9)
                .Integer(0x174841BC, 0x81);
            for (TestParams& p : a)
                call(0x005B2880, CFunc_ApplyRGBAToTexture, p);
            for (float angle : {0.0f, 45.0f, -91.5f, 1234.5678f}) {
                TestParams p; p.Checksum(0, 0x7E7E0002).Float(0xFF7EBAF6, angle);
                call(0x005B26B0, CFunc_RotateTexture, p);
            }
            TestParams r; r.Checksum(0, 0x7E7E0005).Float(0xFF7EBAF6, 3.0f);
            call(0x005B26B0, CFunc_RotateTexture, r);
        }
        // HSVtoRGB, RGBtoHSV
        {
            static TestArray colours[30];
            int hsv[][4] = {{0, 90, 60, -1}, {30, 45, 30, 77}, {359, 90, 60, -1}, {360, 90, 60, -1}, {-30, 60, 40, -1},
                            {-400, 10, 10, 5}, {725, 0, 0, -1}, {120, -5, -5, -1}, {240, 200, 200, -1},
                            {300, 89, 59, 128}, {61, 1, 59, -1}, {179, 33, 17, -1}, {105, 45, 3, -1}, {230, 27, 5, -1},
                            {270, 45, 30, -1}};
            int rgb[][4] = {{128, 0, 0, -1}, {0, 128, 0, 9}, {0, 0, 128, -1}, {0, 0, 0, -1}, {64, 64, 64, -1},
                            {128, 64, 0, -1}, {20, 100, 60, 3}, {90, 10, 127, -1}, {1, 2, 3, -1}, {127, 127, 1, -1},
                            {50, 128, 50, -1}, {200, -5, 7, -1}, {3, 9, 5, -1}, {9, 3, 3, -1}, {0, 7, 7, -1}};
            for (int i = 0; i < 15; ++i) {
                for (int kind = 0; kind < 2; ++kind) {
                    int* v = kind ? rgb[i] : hsv[i];
                    TestArray& array = colours[i * 2 + kind];
                    if (v[3] >= 0)
                        array.Set(Script::ESYMBOLTYPE_INTEGER, {static_cast<uint32_t>(v[0]), static_cast<uint32_t>(v[1]),
                                                                 static_cast<uint32_t>(v[2]), static_cast<uint32_t>(v[3])});
                    else
                        array.Set(Script::ESYMBOLTYPE_INTEGER, {static_cast<uint32_t>(v[0]), static_cast<uint32_t>(v[1]),
                                                                 static_cast<uint32_t>(v[2])});
                    TestParams p; p.Array(kind ? 0xDECB67C7 : 0xFB0E4249, array.Get());
                    UseStandInArrayPool();
                    if (kind)
                        call(0x005B2CD0, CFunc_RGBtoHSV, p);
                    else
                        call(0x005B2970, CFunc_HSVtoRGB, p);
                }
            }
        }
        // FindNearestRailPoint
        {
            alignas(16) static float pos[4] = {0.0f, 1.5f, 2.5f, 3.5f};
            float cases[][2] = {{100.0f, 1.0e8f}, {30000.0f, 50.0f}, {50.0f, 30000.0f}, {30000.0f, 40000.0f},
                                {50.0f, 50.0f}, {22499.0f, 22501.0f}, {22600.0f, 1.0e8f}, {50000.0f, 50.0f}};
            for (auto& c : cases) {
                g_tRailDistance = c[0];
                g_tGeometryDistance = c[1];
                TestParams a; a.Vector(0x7F261953, pos);
                call(0x005B2FC0, CFunc_FindNearestRailPoint, a);
                TestParams b; b.Vector(0x7F261953, pos).Flag(0x9C247A94).Float(0x213395F1, 200.5f)
                    .Checksum(0x288A6EFE, 0x4A110009).Integer(0xE4D305A7, 3);
                call(0x005B2FC0, CFunc_FindNearestRailPoint, b);
            }
        }
        // GetParkName, SetSpecialBarColors, initials, fireball, slap
        {
            call(0x005B3160, CFunc_GetParkName, none);
            TestParams s0; s0.Integer(0x5B8AB877, 0);
            call(0x005B3180, CFunc_SetSpecialBarColors, s0);
            TestParams s1; s1.Integer(0x5B8AB877, 1);
            call(0x005B3180, CFunc_SetSpecialBarColors, s1);
            call(0x005B31C0, CFunc_GetInitialsString, none);
            TestParams i0; i0.String(0x61414D56, "KJM");
            call(0x005B31F0, CFunc_SetInitialsString, i0);
            call(0x005B31F0, CFunc_SetInitialsString, none);
            for (int level : {0, 3, -2}) {
                g_tFireball = level;
                call(0x005B3230, CFunc_GetFireballLevel, none);
            }
            for (uint32_t mode : {0xF9D5D933u, 0xCA1F360Fu, 0x12345678u}) {
                g_tModeName = mode;
                call(0x005B3470, CFunc_InSlapGame, none);
            }
        }
        // GetSkaterLastNameString
        {
            static TestParams entries[4];
            static TestArray list;
            for (TestParams& e : entries)
                e.Reset();
            entries[0].Checksum(0xA1DC81F9, 0x5CA7E001).String(0x37F745A5, "Hawk");
            entries[1].Checksum(0xA1DC81F9, 0x5CA7E002);
            entries[2].Checksum(0xA1DC81F9, 0x5CA7E003).String(0x37F745A5, "Mullen");
            entries[3].Checksum(0xA1DC81F9, 0x5CA7E002).String(0x37F745A5, "Second"); // (after one without)
            list.Set(Script::ESYMBOLTYPE_STRUCTURE, {reinterpret_cast<uint32_t>(entries[0].Struct()),
                                                     reinterpret_cast<uint32_t>(entries[1].Struct()),
                                                     reinterpret_cast<uint32_t>(entries[2].Struct()),
                                                     reinterpret_cast<uint32_t>(entries[3].Struct())});
            g_globalArrays[0xF31E8FF8] = list.Get();
            for (uint32_t name : {0x5CA7E001u, 0x5CA7E002u, 0x5CA7E003u, 0x5CA7E004u}) {
                TestParams p; p.Checksum(0, name);
                call(0x005B3260, CFunc_GetSkaterLastNameString, p);
            }
        }
        // DrawDirectionalLightLines
        {
            TestParams p; p.Float(0xFD4BC03E, 30.0f).Float(0xD8604126, 45.0f).Integer(0x93F60062, 1)
                .Integer(0xFE2BE489, 2).Integer(0x8E411006, 3);
            call(0x005B3300, CFunc_DrawDirectionalLightLines, p);
        }
        traces[run] = g_trace;
    }
    RestoreGlobals();
    DumpTraces(traces[0], traces[1]);
    Compare(traces[0], traces[1], "face textures, texture changes, HSV / RGB, rail points, initials, slap (0x005B2420-0x005B34A0)");
}

} // namespace

SELFTEST(Early, TestTextureCFuncs, Gs4Tests::RestoreGlobals);
