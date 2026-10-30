// Self-tests for the script commands 0x005B4260-0x005B4F80 (the theme's colours, a board logo, preferences from a
// menu, the skater's velocity, bail streams): each original against ours on identical stand-in state; the skate
// module, the skater, the preferences, the matrix and vector maths, the profile templates, Rnd, sprintf / strcmp
// (the game's C runtime isn't started offline), streams and RunScript are stood in for by recording code.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "game/skate/skate_module.h"
#include "game/front/theme_cfuncs.h"
#include "game/mth/matrix.h"
#include "game/mth/random.h"
#include "game/mth/vector.h"
#include "game/pcm/music_api.h"
#include "game/script/game_util_cfuncs.h"
#include "game/script/preferences.h"
#include "game/skate/profile_manager.h"
#include "game/skater/skater.h"
#include "game/skater/skater_misc_cfuncs.h"
#include "game/core/crc.h"
#include "hook/gs4_testkit.h"

namespace {

using namespace Gs4Tests;

alignas(16) uint8_t g_sSkate[0x100];
alignas(16) uint8_t g_sSkater[0x200];
alignas(16) uint8_t g_sPrefs[0x20];
alignas(16) uint8_t g_sProfileManager[0x40];
alignas(16) uint8_t g_sProfile[0x64];
TestParams g_sProfileInfo;
uint32_t g_sRnd;
uint32_t g_sExistingStream;

// Arrays that keep their elements (for the shuffle): the stand-in pool's buffers, each with its own store.
uint32_t g_sElements[8][16];
uint32_t* Elements(uint32_t array)
{
    uint32_t id = ArrayId(array);
    return (id >> 16) == 0xA770 ? g_sElements[(id & 0xFFFF) / 16] : nullptr;
}

void StandInSkaterCalls(StandIns& s)
{
    using AddCot = void (CStruct::*)(uint32_t, uint32_t, uint32_t);
    s.At({0x004783C0, AddressOf(static_cast<AddCot>(&CStruct::AddComponentOfType))}, 12, [](Regs& r) {
        Record({0x83C0, ArrayId(r.ecx), r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x005BA3B0, AddressOf(&Mem::Pool::Contains)}, 4, [](Regs& r) {
        uint32_t id = ArrayId(r.args[0]);
        Record({0xA3B0, r.ecx == reinterpret_cast<uint32_t>(g_arrayPool), id});
        return (id >> 16) == 0xA770 ? 1u : 0u;
    });
    s.At({0x005BA400, AddressOf(&Mem::Pool::Free)}, 4, [](Regs& r) {
        Record({0xA400, r.ecx == reinterpret_cast<uint32_t>(g_arrayPool), ArrayId(r.args[0])});
        return 0u;
    });
    // arrays that store (stood in again over the kit's recording ones)
    s.At({0x0046CC50, AddressOf(&CArray::SetSizeAndType)}, 8, [](Regs& r) {
        Record({0xCC50, ArrayId(r.ecx), r.args[0], r.args[1] & 0xFF});
        if (uint32_t* e = Elements(r.ecx))
            std::memset(e, 0, 16 * 4);
        return 0u;
    });
    s.At({0x0046CB10, AddressOf(&CArray::SetElement)}, 8, [](Regs& r) {
        Record({0xCB10, ArrayId(r.ecx), r.args[0], r.args[1]});
        if (uint32_t* e = Elements(r.ecx))
            e[r.args[0] & 15] = r.args[1];
        return 0u;
    });
    s.At({0x0046CC30, AddressOf(&CArray::GetElement)}, 4, [](Regs& r) {
        if (uint32_t* e = Elements(r.ecx))
            return e[r.args[0] & 15];
        // (an array built by hand: its own elements)
        auto* array = reinterpret_cast<CArray*>(r.ecx);
        return array->m_size == 1 ? array->m_single : array->m_elements[r.args[0]];
    });
    s.At({0x00477140, AddressOf(&Script::CleanUpArray)}, 0, [](Regs& r) {
        Record({0x7140, ArrayId(r.args[0])});
        return 0u;
    });
    s.At({0x0046CAB0, AddressOf(&CArray::Clear)}, 0, [](Regs& r) {
        Record({0xCAB0, ArrayId(r.ecx)});
        return 0u;
    });
    s.At({0x005665C0, AddressOf(&Mdl::Skate::GetSkater)}, 4, [](Regs& r) {
        Record({0x65C0, r.ecx == reinterpret_cast<uint32_t>(g_sSkate), r.args[0]});
        return reinterpret_cast<uint32_t>(g_sSkater);
    });
    s.At({0x00581260, AddressOf(&Obj::CSkater::ShowBoardElement)}, 4, [](Regs& r) {
        Record({0x1260, r.ecx == reinterpret_cast<uint32_t>(g_sSkater), r.args[0]});
        return 0u;
    });
    s.At({0x00565D40, AddressOf(&Mdl::Skate::GetPreferencesFromParams)}, 0, [](Regs& r) {
        Record({0x5D40}); // (the original's second argument isn't passed by ours: it isn't read)
        return reinterpret_cast<uint32_t>(g_sPrefs);
    });
    s.At({0x0046C5D0, AddressOf(&Prefs::Preferences::SetPreference)}, 8, [](Regs& r) {
        Record({0xC5D0, r.ecx == reinterpret_cast<uint32_t>(g_sPrefs), r.args[0], ArrayId(r.args[1])});
        return 1u;
    });
    s.At({0x00626EFB}, 0, [](Regs& r) { // strcmp
        return static_cast<uint32_t>(std::strcmp(reinterpret_cast<const char*>(r.args[0]),
                                                 reinterpret_cast<const char*>(r.args[1])));
    });
    s.At({0x006234FE}, 0, StandInSprintf);
    // maths
    s.At({0x004023F0, AddressOf(&Mth::Vector::Normalize)}, 0, [](Regs& r) {
        auto* v = reinterpret_cast<float*>(r.ecx);
        Record({0x23F0, Bits(v[0]), Bits(v[1]), Bits(v[2]), Bits(v[3])});
        v[0] = 0.6f;
        v[1] = 0.0f;
        v[2] = -0.8f;
        return r.ecx;
    });
    s.At({0x0041A640, AddressOf(&Mth::Matrix::Ident)}, 0, [](Regs& r) {
        Record({0xA640});
        auto* m = reinterpret_cast<float*>(r.ecx);
        for (int i = 0; i < 16; ++i)
            m[i] = i % 5 == 0 ? 1.0f : 0.0f;
        return r.ecx;
    });
    s.At({0x00404E20, AddressOf(&Mth::CreateRotateYMatrix)}, 0, [](Regs& r) {
        Record({0x4E20, r.args[1]});
        auto* m = reinterpret_cast<float*>(r.args[0]);
        for (int i = 0; i < 16; ++i)
            m[i] = static_cast<float>(i) * 0.125f - 0.5f;
        return r.args[0];
    });
    s.At({0x005BFA8C}, 12, [](Regs& r) { // D3DXMatrixMultiply
        auto* out = reinterpret_cast<float*>(r.args[0]);
        auto* a = reinterpret_cast<float*>(r.args[1]);
        auto* b = reinterpret_cast<float*>(r.args[2]);
        Record({0xFA8C, r.args[0] == r.args[1], HashBytes(reinterpret_cast<uint8_t*>(a), 64),
                HashBytes(reinterpret_cast<uint8_t*>(b), 64)});
        for (int i = 0; i < 16; ++i)
            out[i] = a[i] * 0.5f + b[i];
        return r.args[0];
    });
    s.At({0x0040AF60, AddressOf(&Mth::Matrix::Transform)}, 8, [](Regs& r) {
        auto* out = reinterpret_cast<float*>(r.args[0]);
        auto* in = reinterpret_cast<float*>(r.args[1]);
        Record({0xAF60, HashBytes(reinterpret_cast<uint8_t*>(r.ecx), 64), Bits(in[0]), Bits(in[1]), Bits(in[2])});
        out[0] = in[0] * 3.0f + 1.0f;
        out[1] = in[1] - 7.5f;
        out[2] = in[2] * -2.0f;
        out[3] = 1.0f;
        return r.args[0];
    });
    using MatrixAssign = Mth::Matrix& (Mth::Matrix::*)(const Mth::Matrix&);
    s.At({0x00404090, AddressOf(static_cast<MatrixAssign>(&Mth::Matrix::operator=))}, 4, [](Regs& r) {
        Record({0x4090, HashBytes(reinterpret_cast<uint8_t*>(r.args[0]), 64)});
        std::memcpy(reinterpret_cast<void*>(r.ecx), reinterpret_cast<void*>(r.args[0]), 64);
        return r.ecx;
    });
    // streams
    s.At({0x00403DE0, AddressOf(&Mth::Rnd)}, 0, [](Regs& r) {
        Record({0x3DE0, r.args[0]});
        g_sRnd = g_sRnd * 1103515245u + 12345u;
        return r.args[0] ? (g_sRnd >> 8) % r.args[0] : 0u;
    });
    s.At({0x0057B7E0, AddressOf(&Obj::CPlayerProfileManager::GetTemplate)}, 4, [](Regs& r) {
        Record({0xB7E0, r.ecx == reinterpret_cast<uint32_t>(g_sProfileManager), r.args[0]});
        return reinterpret_cast<uint32_t>(g_sProfile);
    });
    s.At({0x00450500, AddressOf(static_cast<bool(__cdecl*)(uint32_t)>(&Pcm::StreamExists))}, 0, [](Regs& r) {
        Record({0x0500, r.args[0]});
        return r.args[0] == g_sExistingStream ? 1u : 0u;
    });
    using RunScriptFn = void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*);
    s.At({0x00475790, AddressOf(static_cast<RunScriptFn>(&Script::RunScript))}, 0, [](Regs& r) {
        Record({0x5790, r.args[0], ArrayId(r.args[1]), r.args[2], r.args[3] & 0xFF, r.args[4]});
        return 0u;
    });
}

void TestSkaterMiscCFuncs()
{
    SaveGlobals({{0x007CE478, 4, {}}, {0x007CE4B4, 4, {}}});
    auto* standIns = new StandIns;
    StandInStructCalls(*standIns);
    StandInSkaterCalls(*standIns);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        GameVariable<void*>(0x007CE478) = g_sSkate;
        GameVariable<void*>(0x007CE4B4) = g_sProfileManager;
        g_own.Reset();
        auto call = [&](uint32_t address, CFunc ours, TestParams& params, void* object = nullptr) {
            Call(original, address, ours, params, object);
        };
        using namespace CFuncs;
        // the theme's colours
        {
            struct Theme {
                uint32_t address;
                CFunc ours;
            };
            const Theme themes[] = {
                {0x005B4260, CFunc_Theme_GetHighlightedTextColor}, {0x005B4290, CFunc_Theme_GetUnhighlightedTextColor},
                {0x005B42C0, CFunc_Theme_GetHighlightedTextColorNew},
                {0x005B42F0, CFunc_Theme_GetUnhighlightedTextColorNew}, {0x005B4320, CFunc_Theme_GetHighlightBarColor},
                {0x005B4350, CFunc_Theme_GetSpeechBoxColor}, {0x005B4380, CFunc_Theme_GetPauseBarColor},
                {0x005B43B0, CFunc_Theme_GetIconOnColor}, {0x005B43E0, CFunc_Theme_GetFontColors},
                {0x005B4410, CFunc_Theme_GetSupportPieceColor}, {0x005B4440, CFunc_Theme_GetAltColor},
                {0x005B4470, CFunc_Theme_GetMenuTitleColor}, {0x005B44A0, CFunc_Theme_GetHUDColor},
                {0x005B44D0, CFunc_Theme_GetHUDSpecialColor}, {0x005B4500, CFunc_Theme_GetBalanceMeterColor},
                {0x005B4530, CFunc_Theme_GetBalanceArrowColor}, {0x005B4560, CFunc_Theme_GetBGPartsColor},
                {0x005B4590, CFunc_Theme_GetKeyColor}, {0x005B45C0, CFunc_Theme_GetTextTwitchColor},
            };
            uint32_t name = 0x0C010000;
            for (const Theme& t : themes) {
                TestParams p; p.Checksum(0x3B3F0BA7, ++name);
                call(t.address, t.ours, p);
            }
        }
        // ApplyCustomLogoToPart
        {
            TestParams p; p.Checksum(0xB6F08F39, 0x10C0FACE).Integer(0x67E6859A, 1);
            call(0x005B45F0, CFunc_ApplyCustomLogoToPart, p);
        }
        // SetPreferencesFromUI
        for (const char* field : {"time_limit", "horse_time_limit", "target_score", "other", "time_limit2"}) {
            for (int variant = 0; variant < 3; ++variant) {
                TestParams p; p.String(0x61414D56, "Two Minutes").String(Crc::GenerateCRCFromString("field"), field);
                if (variant != 1)
                    p.Checksum(0x21902065, 0x0C0FFEE0 + variant);
                p.Integer(0x906B67BA, 120 + variant);
                if (variant == 2)
                    p.Integer(0xCD66C8AE, 50000);
                UseStandInArrayPool();
                UseStandInStructPool();
                call(0x005B46A0, CFunc_SetPreferencesFromUI, p);
            }
        }
        // GetSkaterVelocity
        {
            call(0x005B4890, CFunc_GetSkaterVelocity, g_own); // no object (any params)
            auto* object = reinterpret_cast<Obj::CCompositeObject*>(g_sSkater);
            float velocities[][3] = {{80.5f, 3.25f, -61.75f}, {50.0f, 900.0f, 40.0f}, {-100.0f, 0.0f, 0.5f},
                                     {0.0f, -12.0f, 0.0f}, {70.0f, 1.0f, 71.4f}};
            for (int i = 0; i < 16; ++i)
                reinterpret_cast<float*>(&object->m_matrix)[i] = static_cast<float>(i) * 0.25f - 1.5f;
            for (auto& v : velocities) {
                object->m_vel.x = v[0];
                object->m_vel.y = v[1];
                object->m_vel.z = v[2];
                object->m_vel.w = 1.0f;
                for (int variant = 0; variant < 3; ++variant) {
                    TestParams p;
                    if (variant >= 1)
                        p.Float(0x5582FE27, variant == 1 ? 30.0f : -12.5f);
                    if (variant == 2)
                        p.Float(0x13B9DA7B, 2.75f);
                    call(0x005B4890, CFunc_GetSkaterVelocity, p, g_sSkater);
                }
            }
        }
        // PlaySkaterStream
        {
            auto* skater = reinterpret_cast<Obj::CSkater*>(g_sSkater);
            std::memcpy(skater->m_firstName, "Tony", 5);
            skater->m_name = 0x0A70A700;
            static TestParams voices[3];
            static TestArray voiceList;
            for (TestParams& v : voices)
                v.Reset();
            voices[0].Checksum(0x40C698AF, 0x707CE001).String(0x1A6BE294, "Voice1");
            voices[1].Checksum(0x40C698AF, 0x707CE002).String(0x1A6BE294, "Voice2");
            voices[2].Checksum(0x40C698AF, 0x707CE003).String(0x1A6BE294, "Voice3");
            voiceList.Set(Script::ESYMBOLTYPE_STRUCTURE, {reinterpret_cast<uint32_t>(voices[0].Struct()),
                                                          reinterpret_cast<uint32_t>(voices[1].Struct()),
                                                          reinterpret_cast<uint32_t>(voices[2].Struct())});
            g_globalArrays[0xCA1DB456] = voiceList.Get();
            struct Case {
                bool pro;
                uint32_t voice; // 0: none
                int male;
                const char* exists;
                int count; // -1: the default
            };
            const Case cases[] = {{true, 0, 1, "Hawk_Bail07", -1}, {false, 0x707CE002, 1, "Voice2_Bail03", 5},
                                  {false, 0, 1, "customM_Bail01", 3}, {false, 0, 0, "customF_Bail12", 12},
                                  {true, 0, 0, "nothing", 4}, {false, 0, 0, "customF_Bail01", 0},
                                  {false, 0, 2, "customF_Bail02", 4}};
            for (const Case& c : cases) {
                skater->m_pro = c.pro;
                g_sProfileInfo.Reset();
                g_sProfileInfo.String(0x37F745A5, "Hawk");
                if (c.voice)
                    g_sProfileInfo.Checksum(0x1804A7C4, c.voice);
                std::memset(g_sProfile, 0, sizeof(g_sProfile));
                std::memcpy(g_sProfile + 0x5C, g_sProfileInfo.storage, 8);
                g_sExistingStream = Crc::GenerateCRCFromString(c.exists);
                g_sRnd = 77;
                TestParams p; p.String(0x7321A8D6, "Bail").Integer(0x3F813177, c.male);
                if (c.count >= 0)
                    p.Integer(0x58492707, c.count);
                UseStandInArrayPool();
                UseStandInStructPool();
                call(0x005B4AF0, CFunc_PlaySkaterStream, p, g_sSkater);
            }
        }
        traces[run] = g_trace;
    }
    RestoreGlobals();
    DumpTraces(traces[0], traces[1]);
    Compare(traces[0], traces[1], "theme colours, board logo, preferences, velocity, bail streams (0x005B4260-0x005B4F80)");
}

} // namespace

SELFTEST(Early, TestSkaterMiscCFuncs, Gs4Tests::RestoreGlobals);
