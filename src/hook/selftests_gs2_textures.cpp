// Self-tests for the script commands 0x005A9D80-0x005AAEEF (texture_cfuncs.cpp, profile_cfuncs.cpp): each original
// against ours on the same params, the texture dictionaries, textures, profiles and models stood in for by
// recording code.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/gfx/texture_cfuncs.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skate/profile_cfuncs.h"
#include "game/skate/profile_manager.h"
#include "hook/cfunc_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using namespace CFuncTests;
using Script::CStruct;

uint32_t g_textureFor[4];   // the texture a checksum lookup finds (by the checksum's low 2 bits; 0 = none)
uint32_t g_textureByName;   // the texture a name lookup finds
uint32_t g_loadResult;
uint32_t g_objectFound;
uint16_t g_textureSize[2] = {0x80, 0x40};

void StandInTextureCalls(StandIns& s)
{
    s.At({0x0049F540}, 4, [](Regs& r) { Record({0xF540, r.ecx, r.args[0]}); return g_textureFor[r.args[0] & 3]; });
    s.At({0x0049F560}, 4, [](Regs& r) {
        Record({0xF560, r.ecx, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return g_textureByName;
    });
    s.At({0x0049F500}, 4, [](Regs& r) { Record({0xF500, r.ecx, r.args[0]}); return 1u; });
    s.At({0x0049F9B0}, 16, [](Regs& r) {
        Record({0xF9B0, r.ecx, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1] & 0xFF, r.args[2] & 0xFF,
                r.args[3] & 0xFF});
        return g_loadResult;
    });
    s.At({0x0049FAA0}, 28, [](Regs& r) {
        Record({0xFAA0, r.ecx, r.args[0], r.args[1], r.args[2], r.args[3], r.args[3] <= 8 ? r.args[4] : 0,
                r.args[5] & 0xFF, r.args[6] & 0xFF});
        return g_loadResult;
    });
    s.At({0x0049FBE0}, 8, [](Regs& r) { Record({0xFBE0, r.ecx, r.args[0], r.args[1]}); return 1u; });
    s.At({0x0049FC20}, 16, [](Regs& r) {
        Record({0xFC20, r.ecx, r.args[0], r.args[1], r.args[2], r.args[3] & 0xFF});
        return 1u;
    });
    // a texture's thunks: their argument bytes
    struct Thunk {
        uint32_t address;
        uint16_t pop;
    };
    for (Thunk t : {Thunk{0x0048D060, 8}, Thunk{0x0048DE30, 8}, Thunk{0x0048DE60, 0}, Thunk{0x0048DE70, 0},
                    Thunk{0x0048DE80, 8}, Thunk{0x0048DEB0, 8}, Thunk{0x0048DEE0, 28}, Thunk{0x0049F370, 12},
                    Thunk{0x0049F380, 8}, Thunk{0x0049F390, 16}, Thunk{0x0049F420, 8}, Thunk{0x0049F3B0, 0}}) {
        uint32_t address = t.address;
        uint16_t pop = t.pop;
        s.At({address}, pop, [address, pop](Regs& r) {
            Record({address, r.ecx});
            for (int i = 0; i < pop / 4; ++i) {
                uint32_t a = r.args[i];
                // (bool arguments: only their low byte is set)
                bool isBool = (address == 0x0048D060 || address == 0x0048DE30) ||
                              (address == 0x0049F380 && i == 1) || (address == 0x0049F390 && i == 3) ||
                              (address == 0x0049F420 && i == 1);
                Record({isBool ? a & 0xFF : a});
            }
            return 0u;
        });
    }
    s.At({0x0049F3C0}, 0, [](Regs& r) { Record({0xF3C0, r.ecx}); return 0xABCD0000u | g_textureSize[0]; });
    s.At({0x0049F3D0}, 0, [](Regs& r) { Record({0xF3D0, r.ecx}); return 0xABCD0000u | g_textureSize[1]; });
    s.At({0x00460AD0, AddressOf(&Obj::FindObject)}, 0, [](Regs& r) { Record({0x0AD0, r.args[0]}); return g_objectFound; });
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0x1C30, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
}

uint32_t CallFindInteger(void* code, CStruct* structure, uint32_t value)
{
    uint32_t result;
    __asm {
        push ebx
        mov ebx, structure
        push value
        call code
        add esp, 4
        mov result, eax
        pop ebx
    }
    return result;
}

void TestTextureCFuncs()
{
    std::vector<Saved> saved = {{0x00700F1C, 8, {}}, {0x007CE441, 1, {}}, {0x007CED58, 0x40, {}}, {0x006F3624, 4, {}}};
    SaveAll(saved);
    UseEmptySymbolTable();
    auto* standIns = new StandIns;
    StandInTextureCalls(*standIns);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        GameVariable<uint32_t>(0x00700F1C) = 0x7E0D1C00;
        GameVariable<uint32_t>(0x00700F20) = 0x9A2D1C00;
        auto call = [&](uint32_t address, CFunc ours, Params& params) { Call(original, address, ours, params); };
        Params none;
        GameVariable<uint8_t>(0x007CE441) = 0;
        call(0x005A9D80, CFuncs::CFunc_SceAltPt, none);
        Record({GameVariable<uint8_t>(0x007CE441)});

        // FindNameOfValue: names at several depths, a structure inside, a global structure named (none here),
        // too deep
        {
            Params inner;
            inner.Checksum(0x1111, 0xBEEF).Integer(0x2222, 5);
            Params ints0;
            ints0.Integer(0x5454, 9);
            Params outer;
            outer.Integer(0x3333, 7).Structure(0x4444, inner.Struct()).Checksum(0x5555, 0xCAFE).Checksum(0x6666, 0xBEEF);
            for (uint32_t value : {0xBEEFu, 0xCAFEu, 0x1234u, 7u}) {
                for (int depth : {0, 10, 11}) {
                    Record({original ? Original<uint32_t (*)(CStruct*, uint32_t, int)>(0x005A9D90)(outer.Struct(), value, depth)
                                     : CFuncs::FindNameOfValue(outer.Struct(), value, depth)});
                }
            }
            Params early;
            early.Checksum(0x5151, 0xBEEF).Structure(0x5252, inner.Struct()).Structure(0x5353, ints0.Struct());
            for (uint32_t value : {0xBEEFu, 0x0F00u})
                Record({original ? Original<uint32_t (*)(CStruct*, uint32_t, int)>(0x005A9D90)(early.Struct(), value, 0)
                                 : CFuncs::FindNameOfValue(early.Struct(), value, 0)});
            Params ints;
            ints.Integer(0x7777, 3).Checksum(0x8888, 3).Integer(0x9999, 3).Integer(0xAAAA, 4);
            for (uint32_t value : {3u, 4u, 5u})
                Record({CallFindInteger(original ? OriginalFunction(0x005A9E30) : reinterpret_cast<void*>(&CFuncs::FindNameOfIntegerStub),
                                        ints.Struct(), value)});
        }
        // WrapAngle
        for (float angle : {-725.5f, -0.001f, 0.0f, 359.75f, 360.0f, 360.001f, 1000.25f, 1e9f}) {
            float wrapped = original ? Original<float (*)(float)>(0x005A9E80)(angle) : CFuncs::WrapAngle(angle);
            uint32_t bits;
            std::memcpy(&bits, &wrapped, 4);
            Record({bits});
        }
        // CompositeObjectExists
        for (uint32_t found : {0u, 0x0B1E0000u}) {
            g_objectFound = found;
            Params p;
            p.Checksum(0xA1DC81F9, 0x1D);
            call(0x005A9ED0, CFuncs::CFunc_CompositeObjectExists, p);
        }
        // LoadTexture / CreateTexture
        for (uint32_t result : {0u, 0x7E070000u}) {
            g_loadResult = result;
            Params a;
            a.String(0, "textures\\a.img");
            call(0x005A9F00, CFuncs::CFunc_LoadTexture, a);
            Params b;
            b.String(0, "textures\\b.img").Flag(0x3955FF2E);
            call(0x005A9F00, CFuncs::CFunc_LoadTexture, b);
            for (int variant = 0; variant < 6; ++variant) {
                Params c;
                c.Checksum(0, 0x7E01);
                if (variant != 1)
                    c.Integer(0x73E5BAD0, 64);
                if (variant != 2)
                    c.Integer(0x0AB21AF0, 32);
                c.Integer(0xBA0F2AC5, variant == 4 ? 8 : variant == 5 ? 4 : 32);
                if (variant != 5)
                    c.Integer(0x51533ECB, 16);
                call(0x005A9F60, CFuncs::CFunc_CreateTexture, c);
            }
        }
        // the texture commands with a texture and without
        g_textureFor[0] = 0;
        g_textureFor[1] = 0x7E010000;
        g_textureFor[2] = 0x7E020000;
        g_textureFor[3] = 0;
        for (uint32_t id : {0x7E01u, 0x7E03u}) {
            for (int variant = 0; variant < 2; ++variant) {
                // (three structures: a hand-built one holds 16 components at most)
                Params p;
                Params region;
                Params colour;
                p.Checksum(0, id);
                region.Checksum(0, id);
                colour.Checksum(0, id);
                if (variant) {
                    p.Integer(0xA5D7CFAA, 1).Integer(0xEEFAF080, 2).Integer(0xC313FDFF, 1).Integer(0xA04027C9, 1)
                        .Integer(0x94927839, 0).Checksum(0x9FBBDB72, 0x7E02).Float(0x13B9DA7B, 1.5f);
                    region.Integer(0x0FA8972E, 3).Integer(0xB714F04B, 4).Integer(0x73E5BAD0, 5).Integer(0xE679982E, 6)
                        .Integer(0x3D0F162A, 7).Integer(0x39C54BDE, 8);
                    colour.Integer(0x485A0CDB, 9).Integer(0x7AF07905, 10).Integer(0x88CF948C, 11)
                        .Float(0x2689291C, 0.5f).Integer(0xC164842A, 1).Float(0x6E94F918, 0.25f).Float(0x949BC47B, 2.0f);
                } else {
                    p.Integer(0xA5D7CFAA, 2).Integer(0xEEFAF080, 1).Integer(0x94927839, 1).Checksum(0x9FBBDB72, 0x7E03)
                        .Float(0x13B9DA7B, 0.75f).Integer(0xC313FDFF, 2);
                    region.Integer(0x0AB21AF0, 12);
                    colour.Integer(0xC164842A, 0).Float(0xE4F130F4, 0.125f);
                }
                call(0x005AA0C0, CFuncs::CFunc_Generate32BitImage, p);
                call(0x005AA160, CFuncs::CFunc_Put32BitImageIntoTexture, p);
                call(0x005AA1E0, CFuncs::CFunc_FlipTexture, p);
                call(0x005AA270, CFuncs::CFunc_ResizeTextureInto, p);
                call(0x005AA2E0, CFuncs::CFunc_ScaleTexture, p);
                call(0x005AA340, CFuncs::CFunc_AdjustTextureRegion, region);
                call(0x005AA460, CFuncs::CFunc_PullTextureToEdge, colour);
                call(0x005AA500, CFuncs::CFunc_AdjustTextureBrightness, colour);
                call(0x005AA590, CFuncs::CFunc_AdjustTextureHSV, colour);
            }
        }
        // UnloadTexture: by name, by checksum, neither; found or not
        for (uint32_t found : {0u, 0x7E0A0000u}) {
            g_textureByName = found;
            g_textureFor[1] = found;
            Params a;
            a.String(0, "x.img");
            call(0x005AA040, CFuncs::CFunc_UnloadTexture, a);
            Params b;
            b.Checksum(0, 0x7E01);
            call(0x005AA040, CFuncs::CFunc_UnloadTexture, b);
            Params c;
            c.Flag(0x3D92465E);
            call(0x005AA040, CFuncs::CFunc_UnloadTexture, c);
        }
        g_textureFor[1] = 0x7E010000;
        // CopyTexture / CombineTextures
        for (uint32_t source : {0x7E01u, 0x7E03u}) {
            Params a;
            a.Checksum(0x9FBBDB72, source).Checksum(0x941CBBBA, 0x9E9);
            call(0x005AA670, CFuncs::CFunc_CopyTexture, a);
            Params b;
            b.Checksum(0x9FBBDB72, source);
            call(0x005AA670, CFuncs::CFunc_CopyTexture, b);
            for (uint32_t top : {0x7E02u, 0x7E00u}) {
                for (bool named : {false, true}) {
                    for (bool noPalette : {false, true}) {
                        Params c;
                        c.Checksum(0x9FBBDB72, source).Checksum(0xE126E035, top);
                        if (named)
                            c.Checksum(0x941CBBBA, 0xC0B);
                        if (noPalette)
                            c.Flag(0x5905256B);
                        call(0x005AA6E0, CFuncs::CFunc_CombineTextures, c);
                    }
                }
            }
        }
        // particle and SFP textures
        for (uint32_t found : {0u, 0x9A070000u}) {
            for (uint32_t result : {0u, 0x9A080000u}) {
                g_textureByName = found;
                g_loadResult = result;
                Params a;
                a.String(0, "particles\\spark.img");
                call(0x005AA7B0, CFuncs::CFunc_LoadParticleTexture, a);
                Params b;
                b.String(0, "particles\\smoke.img").Flag(0xD5928F25);
                call(0x005AA7B0, CFuncs::CFunc_LoadParticleTexture, b);
                Params c;
                c.String(0, "");
                call(0x005AA7B0, CFuncs::CFunc_LoadParticleTexture, c);
                call(0x005AA830, CFuncs::CFunc_UnloadParticleTexture, a);
                call(0x005AA880, CFuncs::CFunc_LoadSFPTexture, a);
                call(0x005AA8B0, CFuncs::CFunc_UnloadSFPTexture, a);
            }
        }
        // SetParkName
        {
            std::memset(&GameVariable<char>(0x007CED58), 0x55, 0x40);
            Params a;
            a.String(0, "My Park");
            call(0x005AA8F0, CFuncs::CFunc_SetParkName, a);
            Record({Hash(&GameVariable<char>(0x007CED58))});
            call(0x005AA8F0, CFuncs::CFunc_SetParkName, none);
            Record({Hash(&GameVariable<char>(0x007CED58))});
        }
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(saved);
    Check(traces[0].size() > 700, "texture commands: the original reached their calls");
    Compare(traces[0], traces[1], "texture commands (0x005A9D80-0x005AA8F0)");
}

// --- the profile commands ---

alignas(16) uint8_t g_skate[0x40];
alignas(16) uint8_t g_profiles[0x40];
alignas(16) uint8_t g_profileObject[0x80];
alignas(16) uint8_t g_extraProfile[0x80];
bool g_profileMissing;
bool g_isPro;
uint32_t g_arrayResult;

void StandInProfileCalls(StandIns& s)
{
    s.At({0x00564F80}, 4, [](Regs& r) {
        Record({0x4F80, r.ecx, r.args[0] == reinterpret_cast<uint32_t>(g_scriptParams) ? 1u : r.args[0]});
        return g_profileMissing ? 0u : reinterpret_cast<uint32_t>(g_profileObject);
    });
    s.At({0x00586170, AddressOf(&Obj::CSkaterProfile::IsPro)}, 0, [](Regs& r) {
        Record({0x6170, r.ecx == reinterpret_cast<uint32_t>(g_profileObject) ? 1u : r.ecx});
        return g_isPro ? 0x101u : 0x100u;
    });
    s.At({0x004E8F50}, 0, [](Regs& r) { Record({0x8F50, r.args[0], r.args[1], r.args[2]}); return 0u; });
    s.At({0x00584250, AddressOf(&Obj::CSkaterCareer::CountGoals)}, 4, [](Regs& r) {
        Record({0x4250, r.ecx, r.args[0]});
        return 4u;
    });
    s.At({0x005842A0, AddressOf(&Obj::CSkaterCareer::CountTotalGoals)}, 0, [](Regs& r) { Record({0x42A0, r.ecx}); return 12u; });
    s.At({0x00584320, AddressOf(&Obj::CSkaterCareer::CountLevelsWithGoals)}, 0, [](Regs& r) { Record({0x4320, r.ecx}); return 3u; });
    using AddStringFn = void (CStruct::*)(uint32_t, uint32_t, const char*);
    s.At({0x00478390, AddressOf(static_cast<AddStringFn>(&CStruct::AddString))}, 12, [](Regs& r) {
        Record({0x8390, r.ecx == ScriptParams() ? 1u : r.ecx, r.args[0], r.args[1], Hash(reinterpret_cast<const char*>(r.args[2]))});
        return 0u;
    });
    using GetStringFn = const char* (*)(uint32_t);
    s.At({0x00478FD0, AddressOf(static_cast<GetStringFn>(&Script::GetString))}, 0, [](Regs& r) {
        Record({0x8FD0, r.args[0]});
        return reinterpret_cast<uint32_t>(r.args[0] == 0x407BFD24 ? "%d more goals" : "%d more medals");
    });
    s.At({0x006234FE}, 0, StandInSprintf);
    s.At({0x0057B620, AddressOf(&Obj::CPlayerProfileManager::SetCurrentIndex)}, 4, [](Regs& r) {
        Record({0xB620, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x0057BB40, AddressOf(&Obj::CPlayerProfileManager::AddTemplate)}, 4, [](Regs& r) {
        Record({0xBB40, r.ecx, r.args[0]});
        return 0x701u;
    });
    s.At({0x0057B970, AddressOf(&Obj::CPlayerProfileManager::AddExtraProfile)}, 4, [](Regs& r) {
        Record({0xB970, r.ecx, r.args[0]});
        return 0x700u;
    });
    s.At({0x0057B640, AddressOf(&Obj::CPlayerProfileManager::GetProfile)}, 4, [](Regs& r) {
        Record({0xB640, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_profileObject);
    });
    s.At({0x0057B650, AddressOf(&Obj::CPlayerProfileManager::GetExtraProfile)}, 4, [](Regs& r) {
        Record({0xB650, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_extraProfile);
    });
    s.At({0x00488EA0}, 0, [](Regs& r) { Record({0x8EA0}); return r.ecx; });
    s.At({0x004890F0}, 0, [](Regs&) { Record({0x90F0}); return 0u; });
    s.At({0x00488F40}, 8, [](Regs& r) { Record({0x8F40, r.args[0] != 0 ? 1u : 0u, r.args[1] & 0xFF}); return 0u; });
    s.At({0x004893A0}, 4, [](Regs& r) {
        Record({0x93A0, r.ecx - reinterpret_cast<uint32_t>(g_extraProfile),
                r.args[0] == reinterpret_cast<uint32_t>(g_profileObject) ? 1u : 2u});
        return r.ecx;
    });
    s.At({0x00477090, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
        Record({0x7090, r.ecx - reinterpret_cast<uint32_t>(g_extraProfile)});
        return 0u;
    });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Record({0x8540, r.ecx - reinterpret_cast<uint32_t>(g_extraProfile),
                r.args[0] == reinterpret_cast<uint32_t>(g_profileObject) + 0x4C ? 1u : 2u});
        return 0u;
    });
    using GetArrayFn = Script::CArray* (*)(uint32_t);
    s.At({0x00479070, AddressOf(static_cast<GetArrayFn>(&Script::GetArray))}, 0, [](Regs& r) {
        Record({0x9070, r.args[0]});
        return g_arrayResult;
    });
    s.At({0x0048BFA0}, 0, [](Regs& r) { Record({0xBFA0, r.args[0]}); return 0x30DE1000u; });
    s.At({0x004935E0}, 32, [](Regs& r) {
        Record({0x35E0, r.ecx, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], r.args[2], r.args[3],
                r.args[4], r.args[5], r.args[6], r.args[7]});
        return 0u;
    });
    s.At({0x0048C3C0}, 0, [](Regs& r) { Record({0xC3C0, r.args[0]}); return 0u; });
}

void TestProfileCFuncs()
{
    std::vector<Saved> saved = {{0x007CE478, 4, {}}, {0x007CE4B4, 4, {}}, {0x007CEDE0, 12 * 5 + 12, {}},
                                {0x007D1800, 4, {}}, {0x006F3624, 4, {}}};
    // (the swap table's slot before it too: a full table writes there)
    saved.push_back({0x007CEDE0 - 12, 12, {}});
    SaveAll(saved);
    UseEmptySymbolTable();
    auto* standIns = new StandIns;
    StandInProfileCalls(*standIns);
    alignas(16) static uint8_t manager[0x200];

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        std::memset(g_skate, 0, sizeof(g_skate));
        *reinterpret_cast<uint32_t*>(g_skate + 0x20) = 0xCA2EE200;
        GameVariable<void*>(0x007CE478) = g_skate;
        GameVariable<void*>(0x007CE4B4) = g_profiles;
        std::memset(manager, 0, sizeof(manager));
        reinterpret_cast<uint32_t*>(manager)[0x114 / 4] = 0xB0770000;
        GameVariable<void*>(0x007D1800) = manager;
        auto call = [&](uint32_t address, CFunc ours, Params& params) { Call(original, address, ours, params); };
        Params none;

        // SwapMapping: a sequence of swaps (new pairs, existing ones, swapping back, a full table)
        std::memset(reinterpret_cast<void*>(0x007CEDE0 - 12), 0, 12 * 6);
        const uint32_t swaps[][2] = {{1, 2}, {3, 4}, {2, 5}, {1, 2}, {6, 7}, {8, 9}, {10, 11}, {5, 1}, {12, 13}, {3, 3}};
        for (auto& swap : swaps) {
            if (original)
                Original<void (*)(uint32_t, uint32_t)>(0x005AA930)(swap[0], swap[1]);
            else
                CFuncs::SwapMapping(swap[0], swap[1]);
            for (int i = -1; i < 5; ++i) {
                const uint32_t* slot = reinterpret_cast<const uint32_t*>(0x007CEDE0 + 12 * i);
                Record({slot[0] & 0xFF, slot[1], slot[2]});
            }
        }
        call(0x005AAA80, CFuncs::CFunc_AddGoalEditorNodesToLevelNodeArray, none);
        // PreloadMeshes
        {
            static Script::CArray list;
            static uint32_t elements[4];
            Params e[4];
            e[0].Integer(0xF1E3CD22, 1).String(0x1E90C5A9, "models\\a.mdl");
            e[1].Integer(0xF1E3CD22, 0).String(0x1E90C5A9, "models\\b.mdl");
            e[2].String(0x1E90C5A9, "models\\c.mdl");
            e[3].Integer(0xF1E3CD22, 2);
            for (int i = 0; i < 4; ++i)
                elements[i] = reinterpret_cast<uint32_t>(e[i].Struct());
            list.m_type = Script::ESYMBOLTYPE_STRUCTURE;
            list.m_size = 4;
            list.m_elements = elements;
            g_arrayResult = reinterpret_cast<uint32_t>(&list);
            void* code = original ? OriginalFunction(0x005AAAA0) : reinterpret_cast<void*>(&CFuncs::PreloadMeshesStub);
            uint32_t name = 0x1157;
            __asm {
                mov eax, name
                call code
            }
            Record({0xEEEE});
        }
        // CurrentSkaterIsPro
        for (int variant = 0; variant < 3; ++variant) {
            g_profileMissing = variant == 0;
            g_isPro = variant == 2;
            call(0x005AAB50, CFuncs::CFunc_CurrentSkaterIsPro, none);
        }
        // GetGoalsCompleted / GetNextLevelRequirements (goals short, medals short, both reached)
        {
            Params a;
            a.Integer(0, 7);
            call(0x005AAB80, CFuncs::CFunc_GetGoalsCompleted, a);
            call(0x005AAB80, CFuncs::CFunc_GetGoalsCompleted, none);
            static Script::CArray steps;
            static uint32_t elements[8];
            Params e[8];
            // (career: 12 goals, 3 medals) reached: goals 12, medals 3 (equal), goals and medals 5 (one integer)
            e[0].Flag(0x38DBE1D0).Integer(0, 12);
            e[1].Flag(0x23BBA846).Integer(0, 3);
            e[2].Flag(0x56907F8B).Integer(0, 6);   // medals short
            e[3].Flag(0x032314D1).Integer(0, 13);  // goals short (by "goal")
            e[4].Flag(0x38DBE1D0).Integer(0, 20);
            e[5].Flag(0x38DBE1D0).Integer(0, 2).Flag(0x56907F8B);
            e[6].Flag(0x032314D1).Integer(0, 14);  // goals short, then medals short
            e[7].Flag(0x56907F8B).Integer(0, 9);
            for (int i = 0; i < 8; ++i)
                elements[i] = reinterpret_cast<uint32_t>(e[i].Struct());
            steps.m_type = Script::ESYMBOLTYPE_STRUCTURE;
            g_arrayResult = reinterpret_cast<uint32_t>(&steps);
            const int starts[][2] = {{0, 0}, {0, 2}, {0, 3}, {3, 1}, {3, 2}, {5, 3}, {6, 2}, {1, 1}};
            for (auto& range : starts) {
                steps.m_elements = elements + range[0];
                steps.m_size = static_cast<uint32_t>(range[1]);
                call(0x005AAC10, CFuncs::CFunc_GetNextLevelRequirements, none);
            }
        }
        // the profile manager's commands
        {
            Params a;
            a.Integer(0, 2);
            call(0x005AAD70, CFuncs::CFunc_SetCurrentSkaterProfile, a);
            call(0x005AAD70, CFuncs::CFunc_SetCurrentSkaterProfile, none);
            call(0x005AADA0, CFuncs::CFunc_AddSkaterProfile, a);
            Params b;
            b.Checksum(0xA1DC81F9, 0x7E3);
            call(0x005AADC0, CFuncs::CFunc_AddTemporaryProfile, b);
            Params appearance;
            appearance.Integer(0x1, 1);
            for (int variant = 0; variant < 4; ++variant) {
                Params c;
                c.Checksum(0xA1DC81F9, 0x7E4);
                if (variant & 1)
                    c.Structure(0xEFC8944C, appearance.Struct());
                else
                    c.Integer(0x67E6859A, 1);
                if (variant & 2)
                    c.Flag(0x3AF36E6D);
                call(0x005AADF0, CFuncs::CFunc_RememberTemporaryAppearance, c);
            }
        }
        for (uint32_t& word : g_trace) {
            if (word == reinterpret_cast<uint32_t>(g_profiles))
                word = 0x9F0;
            else if (word == reinterpret_cast<uint32_t>(g_skate))
                word = 0x5CA;
        }
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(saved);
    Check(traces[0].size() > 300, "profile commands: the original reached their calls");
    Compare(traces[0], traces[1], "profile commands (0x005AA930-0x005AADF0)");
}

} // namespace

SELFTEST(Early, TestTextureCFuncs, RemoveStandIns);
SELFTEST(Early, TestProfileCFuncs, RemoveStandIns);
