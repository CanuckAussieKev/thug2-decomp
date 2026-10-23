// Self-tests for the engine's script commands 0x00446460-0x00447220 (render debugging, the animation system, the
// light manager, the screen, texture splats, gamma / light values, clipping and fog, render modes, bone positions):
// each original against ours on the same params; every game function they call is stood in for by recording code.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/crc.h"
#include "game/gfx/screen.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/obj/skeleton_component.h"
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
bool g_popResult;
uint32_t g_lightResult;
float g_lightDirection[4];
uint32_t g_colours[3];
uint8_t g_cameras[2][0x100];
bool g_cameraMissing[2];
uint32_t g_object;
uint32_t g_component;
bool g_boneFound;
bool g_recordPosition;
float g_modulation[2][4];

void StandInCalls(StandIns& s)
{
    s.At({0x0048C110}, 0, [](Regs& r) { Record({0xC110, r.args[0], r.args[1]}); return 0u; });
    s.At({0x006234FE}, 0, StandInSprintf);
    s.At({0x00482A30}, 0, [](Regs& r) {
        Record({0x2A30, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1] & 0xFF});
        return 0u;
    });
    s.At({0x004829F0}, 0, [](Regs& r) {
        Record({0x29F0, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1] & 0xFF});
        return 0u;
    });
    s.At({0x00480D10}, 0, [](Regs& r) { Record({0x0D10, r.args[0], r.args[1], r.args[2]}); return 0u; });
    s.At({0x0047EB30}, 0, [](Regs& r) { Record({0xEB30, r.args[0], r.args[1]}); return 0u; });
    s.At({0x00480660}, 0, [](Regs&) { Record({0x0660}); return 0xCAC40000u; });
    s.At({0x00480DB0}, 4, [](Regs& r) { Record({0x0DB0, r.ecx, r.args[0]}); return 0u; });
    s.At({0x00480F20}, 0, [](Regs& r) { Record({0x0F20, r.ecx}); return 0u; });
    s.At({0x0048EB70}, 0, [](Regs& r) { Record({0xEB70, r.args[0], r.args[1]}); return 0u; });
    s.At({0x0048FB60}, 0, [](Regs&) { Record({0xFB60}); return 0u; });
    s.At({0x0048EA00}, 0, [](Regs&) { Record({0xEA00}); return 0u; });
    s.At({0x0048EA40}, 0, [](Regs&) { Record({0xEA40}); return g_popResult ? 0x7701u : 0x7700u; });
    s.At({0x004A0780, AddressOf(&Nx::SetScreenAspectRatio)}, 0, [](Regs& r) { Record({0x0780, r.args[0]}); return 0u; });
    s.At({0x004A0B70}, 0, [](Regs& r) { Record({0x0B70, r.args[0]}); return 0u; });
    s.At({0x0048C1A0, AddressOf(&Nx::SetLetterbox)}, 0, [](Regs& r) { Record({0xC1A0, r.args[0] & 0xFF}); return 0u; });
    using AddPairFn = void (CStruct::*)(uint32_t, float, float);
    s.At({0x00477FC0, AddressOf(static_cast<AddPairFn>(&CStruct::AddPair))}, 12, [](Regs& r) {
        Record({0x7FC0, r.ecx == ScriptParams() ? 1u : r.ecx, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x00491630}, 0, [](Regs& r) {
        Record({0x1630});
        for (int i = 0; i < 8; ++i)
            Record({reinterpret_cast<const uint32_t*>(r.args[0])[i]});
        return 0u;
    });
    s.At({0x004916F0}, 0, [](Regs& r) { Record({0x16F0, r.args[0]}); return 0u; });
    s.At({0x004915A0}, 0, [](Regs&) { Record({0x15A0}); return 0u; });
    s.At({0x0048C140}, 0, [](Regs& r) {
        Record({0xC140});
        for (int i = 0; i < 3; ++i)
            *reinterpret_cast<float*>(r.args[i]) = g_gamma[i];
        return 0u;
    });
    using RemoveFn = void (CStruct::*)(uint32_t);
    s.At({0x004776B0, AddressOf(static_cast<RemoveFn>(&CStruct::RemoveComponent))}, 4, [](Regs& r) {
        Record({0x76B0, r.ecx == ScriptParams() ? 1u : r.ecx, r.args[0]});
        return 0u;
    });
    using AddIntegerFn = void (CStruct::*)(uint32_t, int);
    s.At({0x00477B80, AddressOf(static_cast<AddIntegerFn>(&CStruct::AddInteger))}, 8, [](Regs& r) {
        Record({0x7B80, r.ecx == ScriptParams() ? 1u : r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    using AddFloatFn = void (CStruct::*)(uint32_t, float);
    s.At({0x00477C60, AddressOf(static_cast<AddFloatFn>(&CStruct::AddFloat))}, 8, [](Regs& r) {
        Record({0x7C60, r.ecx == ScriptParams() ? 1u : r.ecx, r.args[0], g_recordPosition ? r.args[1] : 0u});
        return 0u;
    });
    s.At({0x004A0950}, 0, [](Regs& r) {
        Record({0x0950, r.args[0]});
        return g_cameraMissing[r.args[0] & 1] ? 0u : reinterpret_cast<uint32_t>(g_cameras[r.args[0] & 1]);
    });
    s.At({0x00490E00}, 0, [](Regs& r) { Record({0x0E00, r.args[0]}); return 0u; });
    s.At({0x00490E20}, 0, [](Regs& r) { Record({0x0E20, r.args[0]}); return 0u; });
    s.At({0x0048E8F0}, 0, [](Regs& r) { Record({0xE8F0, r.args[0]}); return g_lightResult; });
    s.At({0x0048E930}, 0, [](Regs& r) { Record({0xE930, r.args[0], r.args[1]}); return g_lightResult ^ 1; });
    s.At({0x0048E920}, 0, [](Regs& r) { Record({0xE920, r.args[0]}); return reinterpret_cast<uint32_t>(g_lightDirection); });
    s.At({0x0048E900}, 0, [](Regs& r) {
        Record({0xE900});
        *reinterpret_cast<uint32_t*>(r.args[0]) = g_colours[0];
        return r.args[0];
    });
    s.At({0x0048E950}, 0, [](Regs& r) {
        Record({0xE950, r.args[1]});
        *reinterpret_cast<uint32_t*>(r.args[0]) = g_colours[1 + (r.args[1] & 1)];
        return r.args[0];
    });
    s.At({0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, [](Regs& r) {
        Record({0x19F0, r.ecx, r.args[0]});
        return g_object;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, r.ecx, r.args[0]});
        return g_component;
    });
    s.At({0x00432050, AddressOf(&Obj::CSkeletonComponent::GetBonePosition)}, 8, [](Regs& r) {
        Record({0x2050, r.ecx, r.args[0]});
        auto* position = reinterpret_cast<float*>(r.args[1]);
        position[0] = 1.5f;
        position[1] = -2.75f;
        position[2] = 1000.125f;
        return g_boneFound ? 0x3301u : 0x3300u;
    });
}

void TestEngineCommandsRender()
{
    std::vector<Saved> saved = {{0x007D643A, 1, {}},  {0x006F3624, 4, {}}, {0x00786A7C, 4, {}}, {0x0078733C, 4, {}},
                                {0x006FA730, 8, {}},  {0x0067DCA4, 16, {}}, {0x006F0FB8, 4, {}}};
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
        GameVariable<uint32_t>(0x006F0FB8) = 0x0B1E0000;
        Params none;
        {
            Params a; a.Integer(0xA6247730, 3).Integer(0xD12347A6, 9);
            call(0x00446460, CFuncs::CFunc_DebugRenderIgnore, a);
            call(0x00446460, CFuncs::CFunc_DebugRenderIgnore, none);
        }
        // InitAnimCompressTable: t48, q48, another type
        {
            Params a; a.String(0, "anims\\t48").Checksum(0, 0xC06EAD08);
            call(0x004464C0, CFuncs::CFunc_InitAnimCompressTable, a);
            Params b; b.Checksum(0, 0xC6A56FE3).String(0, "anims\\q48_table");
            call(0x004464C0, CFuncs::CFunc_InitAnimCompressTable, b);
            Params c; c.String(0, "other").Checksum(0, 0x1234);
            call(0x004464C0, CFuncs::CFunc_InitAnimCompressTable, c);
        }
        // InitAnimSystem: each cache type
        for (uint32_t type : {0x50428FBDu, 0x7ADB1481u, 0xEF082878u, 0x99u}) {
            Params a;
            a.Integer(0x83DFF28A, 0x30000 + static_cast<int>(type & 0xF)).Integer(0x7AA8C11C, 77).Checksum(0x891A96A9, type)
                .Integer(0x416BC96B, 12).Integer(0xD14E9A11, 34);
            call(0x00446570, CFuncs::CFunc_InitAnimSystem, a);
        }
        call(0x00446570, CFuncs::CFunc_InitAnimSystem, none);
        // UpdateAnimCache: each flag and combinations (names as values count too)
        {
            const uint32_t flags[] = {0xB0BAB2AE, 0x0C5F37CD, 0x6A8251D1, 0x1A4E0EF9};
            for (int mask = 0; mask < 16; ++mask) {
                Params a;
                for (int i = 0; i < 4; ++i)
                    if (mask & (1 << i))
                        a.Flag(flags[i]);
                call(0x00446650, CFuncs::CFunc_UpdateAnimCache, a);
            }
            Params b; b.Integer(0x1A4E0EF9, 1);
            call(0x00446650, CFuncs::CFunc_UpdateAnimCache, b);
        }
        call(0x004466F0, CFuncs::CFunc_FinishRendering, none);
        {
            Params a; a.Integer(0x50228D9E, 16).Integer(0x701C70AC, 4);
            call(0x00446700, CFuncs::CFunc_InitLightManager, a);
        }
        call(0x00446760, CFuncs::CFunc_UninitLightManager, none);
        call(0x00446770, CFuncs::CFunc_PushWorldLights, none);
        for (bool result : {true, false}) {
            g_popResult = result;
            call(0x00446780, CFuncs::CFunc_PopWorldLights, none);
        }
        // SetScreen: each setting alone, all, none
        {
            Params a; a.Float(0x0F7C3CAA, 1.7777f);
            call(0x00446790, CFuncs::CFunc_SetScreen, a);
            Params b; b.Float(0xFF7EBAF6, 72.5f);
            call(0x00446790, CFuncs::CFunc_SetScreen, b);
            Params c; c.Integer(0xD4328DD3, 5);
            call(0x00446790, CFuncs::CFunc_SetScreen, c);
            Params c1; c1.Integer(0xD4328DD3, 1);
            call(0x00446790, CFuncs::CFunc_SetScreen, c1);
            Params d; d.Integer(0xD4328DD3, 0).Float(0xFF7EBAF6, 1.0f).Float(0x0F7C3CAA, 1.25f);
            call(0x00446790, CFuncs::CFunc_SetScreen, d);
            call(0x00446790, CFuncs::CFunc_SetScreen, none);
            Params e; e.Integer(0x81B6BF17, 0);
            call(0x00446810, CFuncs::CFunc_AllowBoneLOD, e);
            call(0x00446850, CFuncs::CFunc_DebugLine, none);
        }
        // GetPausePosByResolution at 640 and 1280 wide
        for (uint32_t width : {640u, 1280u}) {
            GameVariable<uint32_t>(0x00786A7C) = width;
            call(0x004468E0, CFuncs::CFunc_GetPausePosByResolution, none);
        }
        // KillAllTextureSplats: a box (sizes positive / negative / NaN / missing; with an offset), a texture, all
        {
            Script::CVector offset = {0, 3.0f, -1.5f, -0.0f}; // (z -0: the signs of zero corners show)
            Script::CVector paramsOffset = {0, 1000.0f, 1000.0f, 1000.0f}; // (not read: the offset is the box's)
            const float sizes[][3] = {{10.0f, 4.0f, 2.5f}, {-3.0f, 0.0f, 7.0f}, {1.0e-3f, -0.0f, 3.0e37f},
                                      {std::numeric_limits<float>::quiet_NaN(), 1.0f, -2.0f}, {3.0e37f, 0.3333333f, -6.1f}};
            for (int i = 0; i < 5; ++i) {
                Params box;
                if (i != 1)
                    box.Float(0x0AB21AF0, sizes[i][0]);
                box.Float(0x7E832F08, sizes[i][1]);
                if (i != 2)
                    box.Float(0x73E5BAD0, sizes[i][2]);
                box.Vector(0xA6F5352F, &offset); // (always: without it x, y, z are uninitialised in the original)
                Params a; a.Structure(0xF756B7C5, box.Struct()).Vector(0xA6F5352F, &paramsOffset).Checksum(0x7D99F28D, 0x77);
                call(0x00446930, CFuncs::CFunc_KillAllTextureSplats, a);
            }
            Params b; b.Checksum(0x7D99F28D, 0x7E47);
            call(0x00446930, CFuncs::CFunc_KillAllTextureSplats, b);
            call(0x00446930, CFuncs::CFunc_KillAllTextureSplats, none);
        }
        // GetGammaValues: fractions that truncate (and one whose product needs more than a float)
        {
            g_gamma[0] = 0.555f;
            g_gamma[1] = 0.29f;
            g_gamma[2] = 1.0f / 3.0f;
            call(0x00446C20, CFuncs::CFunc_GetGammaValues, none);
            g_gamma[0] = -0.4999f;
            g_gamma[1] = 2.5f;
            g_gamma[2] = 0.07f;
            call(0x00446C20, CFuncs::CFunc_GetGammaValues, none);
        }
        // GetDynamicLightModulationFactor: ambient, directional 2, directional without an index
        {
            GameVariable<float>(0x0067DCA4) = 0.375f;
            GameVariable<float>(0x0067DCA8) = 1.625f;
            for (int i = 0; i < 4; ++i) {
                g_modulation[0][i] = 0.25f * static_cast<float>(i) + 0.125f;
                g_modulation[1][i] = -1.0f - static_cast<float>(i);
            }
            GameVariable<float*>(0x0067DCAC) = g_modulation[0];
            GameVariable<float*>(0x0067DCB0) = g_modulation[1];
            g_recordPosition = true;
            Params a; a.Flag(0xBCEE1D37).Integer(0xB8F8BBE6, 1);
            call(0x00446CD0, CFuncs::CFunc_GetDynamicLightModulationFactor, a);
            Params b; b.Integer(0xB8F8BBE6, 2);
            call(0x00446CD0, CFuncs::CFunc_GetDynamicLightModulationFactor, b);
            call(0x00446CD0, CFuncs::CFunc_GetDynamicLightModulationFactor, none);
        }
        // SetClippingDistances: both cameras, one missing; defaults
        {
            std::memset(g_cameras, 0xEE, sizeof(g_cameras));
            g_cameraMissing[0] = false;
            g_cameraMissing[1] = false;
            Params a; a.Float(CrcOf("Near"), 2.5f).Float(CrcOf("Far"), 8000.0f);
            call(0x00446D70, CFuncs::CFunc_SetClippingDistances, a);
            g_cameraMissing[0] = true;
            Params b; b.Float(CrcOf("Far"), 300.0f);
            call(0x00446D70, CFuncs::CFunc_SetClippingDistances, b);
            g_cameraMissing[0] = false;
            g_cameraMissing[1] = true;
            call(0x00446D70, CFuncs::CFunc_SetClippingDistances, none);
            g_cameraMissing[1] = false;
            for (int i = 0; i < 2; ++i)
                for (int k = 0xB8; k < 0xC0; k += 4)
                    Record({*reinterpret_cast<uint32_t*>(&g_cameras[i][k])});
        }
        // the fog distance and colour, render modes, with distance clipping off and on
        for (bool clipping : {false, true}) {
            GameVariable<bool>(0x007D643A) = clipping;
            GameVariable<uint32_t>(0x0078733C) = 0x12345678;
            Params a; a.Float(CrcOf("distance"), 1500.0f);
            call(0x00446DF0, CFuncs::CFunc_SetFogDistance, a);
            Params b; b.Integer(CrcOf("r"), 0x1A0).Integer(CrcOf("g"), 0x20).Integer(CrcOf("b"), -3).Integer(CrcOf("a"), 0x344);
            call(0x00446E50, CFuncs::CFunc_SetFogColor, b);
            Record({GameVariable<uint32_t>(0x0078733C)});
            Params c; c.Integer(CrcOf("g"), 0x40);
            call(0x00446E50, CFuncs::CFunc_SetFogColor, c);
            Record({GameVariable<uint32_t>(0x0078733C)});
        }
        GameVariable<bool>(0x007D643A) = false;
        {
            Params a; a.Integer(0x6835B854, 3);
            call(0x00446F40, CFuncs::CFunc_SetRenderMode, a);
            Params b; b.Integer(0x6835B854, 2);
            call(0x00446F70, CFuncs::CFunc_SetWireframeMode, b);
            Record({GameVariable<uint32_t>(0x006FA730), GameVariable<uint32_t>(0x006FA734)});
            call(0x00446F40, CFuncs::CFunc_SetRenderMode, none);
            call(0x00446F70, CFuncs::CFunc_SetWireframeMode, none);
            Record({GameVariable<uint32_t>(0x006FA730), GameVariable<uint32_t>(0x006FA734)});
        }
        // lights: colours (results passed back), the direction, the current colours
        for (uint32_t result : {0x100u, 0x201u}) {
            g_lightResult = result;
            Params a; a.Integer(CrcOf("r"), 0x111).Integer(CrcOf("g"), 0x22).Integer(CrcOf("b"), 0x33);
            call(0x00446FA0, CFuncs::CFunc_SetLightAmbientColor, a);
            Params b; b.Integer(0x7F8C98FE, 1).Integer(0x93F60062, 0x44).Integer(0xFE2BE489, 0x155).Integer(0x8E411006, 0x66);
            call(0x004470A0, CFuncs::CFunc_SetLightDiffuseColor, b);
        }
        {
            g_lightDirection[0] = 0.5f;
            g_lightDirection[1] = -1.25f;
            g_lightDirection[2] = 3.0f;
            g_lightDirection[3] = 1.0f;
            Params a; a.Integer(0x7F8C98FE, 1);
            call(0x00447010, CFuncs::CFunc_GetLightDirection, a);
            g_colours[0] = 0x80112233;
            g_colours[1] = 0x00FFEE01;
            g_colours[2] = 0x7F00807F;
            call(0x00447130, CFuncs::CFunc_GetLightCurrentColor, none);
        }
        // GetBonePosition: found, not found, no skeleton, no object
        {
            Params a; a.Checksum(0x40C698AF, 0x5CA7E).Checksum(0xCAB94088, 0xB0E);
            g_object = 0x0B1E0040;
            g_component = 0x0C0E0000;
            g_boneFound = true;
            call(0x00447220, CFuncs::CFunc_GetBonePosition, a);
            g_boneFound = false;
            call(0x00447220, CFuncs::CFunc_GetBonePosition, a);
            g_component = 0;
            call(0x00447220, CFuncs::CFunc_GetBonePosition, a);
            g_object = 0;
            g_recordPosition = false; // (uninitialised in the original)
            call(0x00447220, CFuncs::CFunc_GetBonePosition, a);
        }
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(saved);
    Check(traces[0].size() > 400, "engine commands (render): the original reached its calls");
    Compare(traces[0], traces[1], "engine commands (render, 0x00446460-0x00447220)");
}

} // namespace

SELFTEST(Early, TestEngineCommandsRender, RemoveStandIns);
