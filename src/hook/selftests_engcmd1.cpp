// Self-tests for the engine's script commands 0x00445B50-0x00446450 (lens flare, terrain, sun, gamma, fog,
// dynamic lighting, UV wibble, texture animation, render mode): each original against ours on the same params;
// every game function they call (and the registry) is stood in for by recording code, so they're compared call by
// call.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/crc.h"
#include "game/script/cfuncs_engine.h"
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

uint32_t CrcOf(const char* name) { return Crc::GenerateCRCFromString(name); }

float g_gamma[3];
uint8_t g_terrainFound;
LSTATUS g_regCreate;

void StandInCalls(StandIns& s)
{
    s.At({0x00442DF0}, 0, [](Regs& r) { Record({0x2DF0, r.args[0], r.args[1]}); return 0u; });
    s.At({0x00443030}, 0, [](Regs& r) { Record({0x3030, r.args[0], r.args[1]}); return 0u; });
    s.At({0x004442C0}, 0, [](Regs& r) { Record({0x42C0, r.args[0]}); return 0x1100u | ((r.args[0] * 7) & 0xFF); });
    s.At({0x00444480}, 0, [](Regs& r) {
        Record({0x4480, r.args[1] & 0xFF, r.args[2]});
        *reinterpret_cast<uint32_t*>(r.args[0]) = 0x57A0000 + (r.args[1] & 0xFF);
        return g_terrainFound ? 0x101u : 0x100u;
    });
    using AddStructureFn = void (CStruct::*)(uint32_t, CStruct*);
    s.At({0x00478670, AddressOf(static_cast<AddStructureFn>(&CStruct::AddStructure))}, 8, [](Regs& r) {
        Record({0x8670, r.ecx == ScriptParams() ? 1u : r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004426D0}, 0, [](Regs& r) {
        auto* props = reinterpret_cast<const uint32_t*>(r.args[0]);
        uint32_t mask = r.args[1];
        Record({0x26D0, mask});
        for (int i = 0; i < 6; ++i)
            if (mask & (1u << (i < 4 ? i : i + 1)))
                Record({props[i]});
        if (mask & 0x80)
            Record({reinterpret_cast<const uint8_t*>(props)[0x40]});
        return 0u;
    });
    s.At({0x005B6090}, 0, [](Regs& r) { Record({0x6090, r.args[0] & 0xFF}); return 0u; });
    s.At({0x0048C140}, 0, [](Regs& r) {
        Record({0xC140});
        for (int i = 0; i < 3; ++i)
            *reinterpret_cast<float*>(r.args[i]) = g_gamma[i];
        return 0u;
    });
    s.At({0x0048C130}, 0, [](Regs& r) { Record({0xC130, r.args[0], r.args[1], r.args[2]}); return 0u; });
    s.At({0x0048E970}, 0, [](Regs& r) { Record({0xE970, r.args[0], r.args[1]}); return 0u; });
    s.At({0x0048E990}, 0, [](Regs& r) { Record({0xE990, r.args[0], r.args[1], r.args[2]}); return 0u; });
    s.At({0x00490DE0}, 0, [](Regs& r) { Record({0x0DE0, r.args[0] & 0xFF}); return 0u; });
    s.At({0x00490E10}, 0, [](Regs& r) { Record({0x0E10, r.args[0]}); return 0u; });
    s.At({0x0048C510}, 0, [](Regs& r) {
        Record({0xC510, r.args[0]});
        return r.args[0] & 0x10000 ? 0u : 0x5EC00000u + (r.args[0] & 0xFFFF);
    });
    s.At({0x0049E820}, 12, [](Regs& r) { Record({0xE820, r.ecx, r.args[0], r.args[1], r.args[2]}); return 0u; });
    s.At({0x0049E830}, 32, [](Regs& r) {
        Record({0xE830, r.ecx});
        for (int i = 0; i < 8; ++i)
            Record({r.args[i]});
        return 0u;
    });
    s.At({0x0049E840}, 4, [](Regs& r) { Record({0xE840, r.ecx, r.args[0] & 0xFF}); return 0u; });
    s.At({0x0049E850}, 8, [](Regs& r) { Record({0xE850, r.ecx, r.args[0], r.args[1]}); return 0u; });
    s.At({0x0049E860}, 0, [](Regs& r) { Record({0xE860, r.ecx}); return 0u; });
    s.At({0x0049E870}, 4, [](Regs& r) { Record({0xE870, r.ecx, r.args[0] & 0xFF}); return 0u; });
    s.At({0x0049E880}, 0, [](Regs& r) { Record({0xE880, r.ecx}); return 0u; });
    s.At({0x0048C0D0}, 0, [](Regs&) { Record({0xC0D0}); return 0u; });
    // the registry (the original through its imports)
    StandInApi(s, 0x0064500C, reinterpret_cast<const void*>(&RegCreateKeyExA), 36, [](Regs& r) {
        Record({0x0CEA, r.args[0], Hash(reinterpret_cast<const char*>(r.args[1])), r.args[2], r.args[3], r.args[4],
                r.args[5], r.args[6], r.args[8]});
        if (g_regCreate == ERROR_SUCCESS)
            *reinterpret_cast<HKEY*>(r.args[7]) = reinterpret_cast<HKEY>(0x4E7A0002);
        return static_cast<uint32_t>(g_regCreate);
    });
    StandInApi(s, 0x00645010, reinterpret_cast<const void*>(&RegSetValueExA), 24, [](Regs& r) {
        Record({0x05E7, r.args[0], Hash(reinterpret_cast<const char*>(r.args[1])), r.args[2], r.args[3],
                *reinterpret_cast<const uint32_t*>(r.args[4]), r.args[5]});
        return 0u;
    });
    StandInApi(s, 0x00645014, reinterpret_cast<const void*>(&RegCloseKey), 4, [](Regs& r) {
        Record({0x0C10, r.args[0]});
        return 0u;
    });
}

void TestEngineCommandsLighting()
{
    std::vector<Saved> saved = {{0x007D643A, 1, {}}, {0x006F3624, 4, {}}};
    SaveAll(saved);
    UseEmptySymbolTable();
    auto* standIns = new StandIns;
    StandInCalls(*standIns);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        auto call = [&](uint32_t address, CFunc ours, Params& params) { Call(original, address, ours, params); };
        GameVariable<bool>(0x007D643A) = false;
        {
            Params none;
            Params a; a.Integer(0x1234, 5).Checksum(0x99, 0x77);
            call(0x00445B50, CFuncs::CFunc_HideLensFlare, a);
            call(0x00445B70, CFuncs::CFunc_UnhideLensFlare, none);
        }
        // GetTerrainTypeParam: by checksum (found), by number (not found), a number that doesn't fit a byte, nothing
        {
            g_terrainFound = 1;
            Params a; a.Checksum(0x5B058376, 0xBA5E).Checksum(0x3789AC4E, 0x4321).Integer(0x75D274BE, 9);
            call(0x00445B90, CFuncs::CFunc_GetTerrainTypeParam, a);
            g_terrainFound = 0;
            Params b; b.Checksum(0x5B058376, 0xBA5F).Integer(0x75D274BE, 0x1FE);
            call(0x00445B90, CFuncs::CFunc_GetTerrainTypeParam, b);
            g_terrainFound = 1;
            Params c; c.Integer(0x75D274BE, 37).Checksum(0x5B058376, 0x1D);
            call(0x00445B90, CFuncs::CFunc_GetTerrainTypeParam, c);
            Params none;
            call(0x00445B90, CFuncs::CFunc_GetTerrainTypeParam, none);
        }
        // SetSunProps: everything and enable; some and disable; one at a time; both flags
        {
            Params a;
            a.Float(0xF4E775F6, 1.5f).Float(0xCD0576EE, -2.25f).Float(0xC48391A5, 300.0f).Float(0x083FDB95, 0.75f)
                .Checksum(0x7D99F28D, 0x7E7).Checksum(0xEB1CCC76, 0xC0C0).Flag(0x0C5F37CD);
            call(0x00445C50, CFuncs::CFunc_SetSunProps, a);
            Params b; b.Float(0xCD0576EE, 4.0f).Checksum(0xEB1CCC76, 0xC0C1).Flag(0xB0BAB2AE);
            call(0x00445C50, CFuncs::CFunc_SetSunProps, b);
            const uint32_t floats[] = {0xF4E775F6, 0xCD0576EE, 0xC48391A5, 0x083FDB95};
            for (uint32_t name : floats) {
                Params c; c.Float(name, 6.5f);
                call(0x00445C50, CFuncs::CFunc_SetSunProps, c);
            }
            Params d; d.Checksum(0x7D99F28D, 0x7E8);
            call(0x00445C50, CFuncs::CFunc_SetSunProps, d);
            Params e; e.Flag(0xB0BAB2AE).Flag(0x0C5F37CD);
            call(0x00445C50, CFuncs::CFunc_SetSunProps, e);
            Params none;
            call(0x00445C50, CFuncs::CFunc_SetSunProps, none);
        }
        // SetProgressive / SetColorBufferClear
        for (int value : {1, 0, -3, 7}) {
            Params a; a.Integer(0xF649D637, value);
            call(0x00445D50, CFuncs::CFunc_SetProgressive, a);
            Params b; b.Integer(0x1A4E0EF9, value);
            call(0x00445D90, CFuncs::CFunc_SetColorBufferClear, b);
        }
        {
            Params none;
            call(0x00445D50, CFuncs::CFunc_SetProgressive, none);
            call(0x00445D90, CFuncs::CFunc_SetColorBufferClear, none);
        }
        // ApplyChangeGamma: each channel raised / lowered inside and at the ends of the range, an unknown colour,
        // the registry key not created
        {
            const uint32_t colours[] = {0x059EA070, 0x2F6511DE, 0x61C9354B, 0x12345678};
            const float starts[][3] = {{0.5f, 0.25f, 0.75f}, {1.0f, 0.0f, 0.99f}, {0.0f, 1.0f, 0.01f}, {1.5f, -0.5f, 2.0f}, {-0.0f, -0.0f, -0.0f}};
            const float changes[] = {0.125f, -0.0625f, 0.0f};
            for (const float* start : starts) {
                for (uint32_t colour : colours) {
                    for (float change : changes) {
                        std::memcpy(g_gamma, start, sizeof(g_gamma));
                        g_regCreate = colour == 0x2F6511DE && change < 0.0f ? ERROR_ACCESS_DENIED : ERROR_SUCCESS;
                        Params a;
                        a.Checksum(CrcOf("color"), colour).Float(CrcOf("change"), change);
                        call(0x00445E60, CFuncs::CFunc_ApplyChangeGamma, a);
                    }
                }
            }
            g_regCreate = ERROR_SUCCESS;
            g_gamma[0] = std::numeric_limits<float>::quiet_NaN();
            Params b; b.Checksum(CrcOf("color"), 0x059EA070).Float(CrcOf("change"), 0.5f);
            call(0x00445E60, CFuncs::CFunc_ApplyChangeGamma, b);
            Params c; c.Checksum(CrcOf("color"), 0x61C9354B).Float(CrcOf("change"), std::numeric_limits<float>::quiet_NaN());
            call(0x00445E60, CFuncs::CFunc_ApplyChangeGamma, c);
        }
        // SetDynamicLightModulationFactor: no value; ambient; directional; directional without its index
        {
            Params a; a.Float(0xC483C21F, 2.0f).Flag(0xBCEE1D37);
            call(0x00446000, CFuncs::CFunc_SetDynamicLightModulationFactor, a);
            Params b; b.Float(0xE288A7CB, 0.5f).Float(0xC483C21F, 0.25f).Flag(0xBCEE1D37).Integer(0xB8F8BBE6, 3);
            call(0x00446000, CFuncs::CFunc_SetDynamicLightModulationFactor, b);
            Params c; c.Float(0xE288A7CB, 0.75f).Float(0xC483C21F, 1.25f).Integer(0xB8F8BBE6, 2);
            call(0x00446000, CFuncs::CFunc_SetDynamicLightModulationFactor, c);
            Params d; d.Float(0xE288A7CB, 3.0f);
            call(0x00446000, CFuncs::CFunc_SetDynamicLightModulationFactor, d);
        }
        // fog, with distance clipping off and on
        for (bool clipping : {false, true}) {
            GameVariable<bool>(0x007D643A) = clipping;
            Params none;
            call(0x004460B0, CFuncs::CFunc_EnableFog, none);
            call(0x004460D0, CFuncs::CFunc_DisableFog, none);
            Params a; a.Float(CrcOf("exponent"), 0.375f);
            call(0x004460F0, CFuncs::CFunc_SetFogExponent, a);
        }
        GameVariable<bool>(0x007D643A) = false;
        // VCWibblePlayFromKeyframe: no sector; a sector not in the scene; everything; only the sector
        {
            Params a; a.Integer(0x69812EAC, 1);
            call(0x00446130, CFuncs::CFunc_VCWibblePlayFromKeyframe, a);
            Params b; b.Checksum(0xB45C2617, 0x10055).Integer(0x906B67BA, 2);
            call(0x00446130, CFuncs::CFunc_VCWibblePlayFromKeyframe, b);
            Params c; c.Checksum(0xB45C2617, 0x0ABC).Integer(0x69812EAC, 4).Integer(0x756F5456, 6).Integer(0x906B67BA, 3);
            call(0x00446130, CFuncs::CFunc_VCWibblePlayFromKeyframe, c);
            Params d; d.Checksum(0xB45C2617, 0x0ABD).Integer(0x756F5456, 8).Integer(0x69812EAC, 2); // (no time: 0)
            call(0x00446130, CFuncs::CFunc_VCWibblePlayFromKeyframe, d);
        }
        // the UV wibble and texture animation commands
        {
            const char* names[] = {"u_vel", "u_amp", "u_freq", "u_phase", "v_vel", "v_amp", "v_freq", "v_phase"};
            Params a; a.Checksum(CrcOf("sector"), 0x0123);
            for (int i = 0; i < 8; ++i)
                a.Float(CrcOf(names[i]), 0.5f + i * 1.25f);
            call(0x004461D0, CFuncs::CFunc_SetUVWibbleParams, a);
            Params b; b.Checksum(CrcOf("sector"), 0x10124); // (a sector not in the scene; the values in reverse order)
            for (int i = 7; i >= 0; --i)
                b.Float(CrcOf(names[i]), -1.0f - i * 0.5f);
            call(0x004461D0, CFuncs::CFunc_SetUVWibbleParams, b);
            Params c; c.Checksum(CrcOf("sector"), 0x0125);
            call(0x004462D0, CFuncs::CFunc_EnableExplicitUVWibble, c);
            call(0x00446300, CFuncs::CFunc_DisableExplicitUVWibble, c);
            Params d; d.Checksum(CrcOf("sector"), 0x0126).Float(CrcOf("u_off"), 0.125f).Float(CrcOf("v_off"), -3.5f);
            call(0x00446330, CFuncs::CFunc_SetUVWibbleOffsets, d);
            call(0x004463A0, CFuncs::CFunc_ResetTextureAnim, c);
            Params e; e.Checksum(CrcOf("sector"), 0x0127).Flag(0xAF6240B2);
            call(0x004463D0, CFuncs::CFunc_StartTextureAnim, e);
            call(0x004463D0, CFuncs::CFunc_StartTextureAnim, c);
            call(0x00446420, CFuncs::CFunc_StopTextureAnim, e);
            Params none;
            call(0x00446450, CFuncs::CFunc_ToggleRenderMode, none);
        }
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(saved);
    Check(traces[0].size() > 400, "engine commands (lighting): the original reached its calls");
    Compare(traces[0], traces[1], "engine commands (lighting, 0x00445B50-0x00446450)");
}

} // namespace

SELFTEST(Early, TestEngineCommandsLighting, RemoveStandIns);
