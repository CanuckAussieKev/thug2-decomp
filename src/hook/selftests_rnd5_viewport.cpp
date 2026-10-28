// Self-tests for the engine's viewports, screen modes and occlusion requests (src/game/gfx/viewport.cpp, RND5-1): the
// original and ours over stand-in cameras (a recording destructor that may clear the smart pointers, as a real one
// does; the angle setters recorded), the PC's CreateViewport, the script structure's getters / AddChecksum, the
// script global getter and Dbg::Printf stood in and recorded; the globals and the arena compared after each step.

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/dbg.h"
#include "game/gfx/camera.h"
#include "game/gfx/pc_viewport.h"
#include "game/gfx/viewport.h"
#include "game/obj/smtptr.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/rnd5_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace Rnd5Tests;

// The cameras' fake vtable (in the arena): [0] the scalar deleting destructor, recorded; on even cameras it clears
// the smart pointers to it first (the object's list at +4: object, prev, next).
uint32_t g_cameraVtable[4];
bool g_clearOnDelete;

void StandInViewportCalls(StandIns& s)
{
    StandInMemory(s);
    g_cameraVtable[0] = reinterpret_cast<uint32_t>(s.Thunk(4, [](Regs& r) {
        Record({0xDE1E, Relocate(r.ecx), r.args[0]});
        if (g_clearOnDelete) {
            auto* pointer = *reinterpret_cast<Obj::CSmtPtr<Obj::CRefCounted>**>(r.ecx + 4);
            while (pointer) {
                Obj::CSmtPtr<Obj::CRefCounted>* next = pointer->mp_next;
                pointer->mp_object = nullptr;
                pointer->mp_prev = nullptr;
                pointer->mp_next = nullptr;
                pointer = next;
            }
            *reinterpret_cast<uint32_t*>(r.ecx + 4) = 0;
        }
        return r.ecx;
    }));
    s.At({0x00485B20, AddressOf(&Gfx::Camera::SetHFOV)}, 4, [](Regs& r) {
        Record({0x5B20, Relocate(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00485AA0, AddressOf(&Gfx::Camera::UpdateAdjustedHFOV)}, 0, [](Regs& r) {
        Record({0x5AA0, Relocate(r.ecx), GameVariable<uint32_t>(0x0067E544)});
        return 0u;
    });
    s.At({0x004CB860, AddressOf(&Nx::CreateViewport)}, 0, [](Regs& r) {
        const auto* rect = reinterpret_cast<const uint32_t*>(r.args[0]);
        Record({0xB860, rect[0], rect[1], rect[2], rect[3], r.args[1]});
        return Bump(0x24);
    });
    // The script global's float (in ST0): a CALL to a recording thunk, then FLD the scripted value; RET.
    static float s_value;
    void* recorder = s.Thunk(0, [](Regs& r) {
        Record({0x8F30, Hash(reinterpret_cast<const char*>(r.args[1]))}); // (args[0]: the stub's own return)
        uint32_t bits = 0x42A00000; // 80
        NextAnswer(&bits);
        s_value = std::bit_cast<float>(bits);
        return 0u;
    });
    for (uint32_t address : {0x00478F30u, AddressOf(static_cast<float (*)(const char*)>(&Script::GetFloat))}) {
        std::vector<uint8_t> code = Branch(0xE8, address, recorder);
        code.insert(code.end(), {0xD9, 0x05});
        std::vector<uint8_t> where = Dword(reinterpret_cast<uint32_t>(&s_value));
        code.insert(code.end(), where.begin(), where.end());
        code.push_back(0xC3);
        s.Bytes(address, code);
    }
    StandInScriptStructure(s);
}

std::initializer_list<std::pair<uint32_t, uint32_t>> kMemory = {
    {0x00701340, 0x10}, // the screen's aspect, the screens' angle, the default angle, the mode
    {0x00701350, 0x28}, // the viewports
    {0x0067E540, 0x10}, // the mode's map, the angle factor, the 2D-in-3D numerator and upper bound
    {0x00701378, 0x0C}, // the active count, the lower bound, the request count
    {0x0067E550, 0x400}, {0x00701390, 0x40}, {0x007D1800, 4}};

constexpr uint32_t kCameraSize = 0xC4;

uint8_t* NewCamera()
{
    auto* camera = reinterpret_cast<uint8_t*>(Bump(kCameraSize));
    std::memset(camera, 0, kCameraSize);
    *reinterpret_cast<uint32_t**>(camera) = g_cameraVtable;
    return camera;
}

// A stand-in viewport (made by Construct, original or ours).
uint8_t* NewViewport(bool original, Lcg& random, uint8_t* camera)
{
    auto* viewport = reinterpret_cast<uint8_t*>(Bump(0x24));
    float rect[4] = {RandomFloat(random, 0, 1), RandomFloat(random, 0, 1), RandomFloat(random, 0.1f, 1),
                     RandomFloat(random, 0.1f, 1)};
    Record({Relocate(At<uint32_t(__thiscall*)(void*, const float*, void*)>(original, 0x004A0DB0)(
        viewport, random.Below(4) ? rect : nullptr, camera))});
    return viewport;
}

// A viewport's life and members, the manager's cameras and modes, the field of view, the closest camera.
void TestViewports()
{
    GameMemory memory(kMemory);
    StandIns& s = *new StandIns();
    StandInViewportCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 60; ++round) {
        ok &= CompareTwoRuns("viewports", reported, [&](bool original) {
            g_trace.clear();
            g_answers.clear();
            ResetArena(0xEE);
            memory.Fill(0x7E5 + round);
            Lcg random(0x7E50 + round);
            GameVariable<uint32_t>(0x007D1800) = Bump(0x10);
            GameVariable<float>(0x00701340) = RandomFloat(random, 0.5f, 2.5f);
            GameVariable<float>(0x00701348) = RandomFloat(random, 40, 100);
            int mode = static_cast<int>(random.Below(6));
            GameVariable<const int*>(0x0067E540) = &GameVariable<int>(0x00649CE0) + 4 * mode;
            GameVariable<int>(0x0070134C) = mode;
            GameVariable<int>(0x00701378) = (&GameVariable<int>(0x00649CC8))[mode];
            std::vector<uint8_t*> cameras;
            for (int i = 0; i < 6; ++i)
                cameras.push_back(NewCamera());
            for (int i = 0; i < 10; ++i) {
                uint8_t* camera = random.Below(4) ? cameras[random.Below(6)] : nullptr;
                (&GameVariable<uint8_t*>(0x00701350))[i] = NewViewport(original, random, camera);
            }
            for (int step = 0; step < 24; ++step) {
                g_clearOnDelete = random.Below(2) != 0;
                auto* viewport = (&GameVariable<uint8_t*>(0x00701350))[random.Below(10)];
                uint8_t* camera = random.Below(5) ? cameras[random.Below(6)] : nullptr;
                switch (random.Below(13)) {
                case 0:
                    Record({At<uint32_t(__thiscall*)(void*, void*)>(original, 0x004A0D50)(viewport, camera) & 0xFF});
                    break;
                case 1:
                    At<void(__thiscall*)(void*)>(original, 0x004A0D70)(viewport);
                    break;
                case 2:
                    Record({std::bit_cast<uint32_t>(At<float(__thiscall*)(void*)>(original, 0x004A0D20)(viewport))});
                    break;
                case 3:
                    At<void(__cdecl*)()>(original, 0x004A0760)();
                    break;
                case 4:
                    Record({Relocate(At<uint32_t(__cdecl*)(int)>(original, 0x004A0950)(
                        static_cast<int>(random.Below(4))))});
                    break;
                case 5: {
                    int view = static_cast<int>(random.Below(4));
                    int index = GameVariable<const int*>(0x0067E540)[view];
                    if (index >= 0 && random.Below(2)) { // (its camera marked first)
                        auto* target = (&GameVariable<uint8_t*>(0x00701350))[index];
                        Record({At<uint32_t(__thiscall*)(void*, void*)>(original, 0x004A0D50)(
                                    target, *reinterpret_cast<void**>(target + 0x18)) & 0xFF});
                    }
                    At<void(__cdecl*)(int, void*)>(original, 0x004A0970)(view, camera);
                    break;
                }
                case 6:
                    Record({Relocate(At<uint32_t(__cdecl*)(int)>(original, 0x004A09C0)(
                        static_cast<int>(random.Below((&GameVariable<int>(0x00649CC8))[GameVariable<int>(0x0070134C)]))))});
                    break;
                case 7: {
                    Mth::Vector position = {AwkwardFloat(random, -900, 900), RandomFloat(random, -900, 900),
                                            RandomFloat(random, -900, 900), 1.0f};
                    for (uint8_t* c : cameras) {
                        auto* pos = reinterpret_cast<float*>(c + 0x5C);
                        pos[0] = random.Below(6) ? RandomFloat(random, -900, 900) : position.x;
                        pos[1] = RandomFloat(random, -900, 900);
                        pos[2] = RandomFloat(random, -900, 900);
                    }
                    if (random.Below(3) == 0) { // (ties: every camera at the same place)
                        for (uint8_t* c : cameras)
                            std::memcpy(c + 0x5C, &position, 12); // (at distance 0: exact)
                    }
                    float distance = 12345.0f;
                    Record({Relocate(At<uint32_t(__cdecl*)(const Mth::Vector*, float*)>(original, 0x004A09E0)(
                                &position, random.Below(3) ? &distance : nullptr)),
                            std::bit_cast<uint32_t>(distance)});
                    break;
                }
                case 8: {
                    int next = static_cast<int>(random.Below(6));
                    g_answers = {1, std::bit_cast<uint32_t>(RandomFloat(random, 30, 120)), 1,
                                 std::bit_cast<uint32_t>(RandomFloat(random, 30, 120)), 1,
                                 std::bit_cast<uint32_t>(RandomFloat(random, 30, 120)), 1,
                                 std::bit_cast<uint32_t>(RandomFloat(random, 30, 120))};
                    At<void(__cdecl*)(int)>(original, 0x004A0AD0)(next);
                    break;
                }
                case 9:
                    At<void(__cdecl*)(float)>(original, 0x004A0B70)(
                        random.Below(3) ? AwkwardFloat(random, 1, 170) : RandomFloat(random, -0.01f, 0.01f));
                    break;
                case 10: {
                    float x = 1, y = 2;
                    int depth = 3;
                    Record({std::bit_cast<uint32_t>(At<float(__thiscall*)(void*, const Mth::Vector*, float*, float*,
                                                                         int*)>(original, 0x004A0CC0)(
                        viewport, nullptr, &x, &y, &depth))});
                    Record({At<uint32_t(__thiscall*)(void*, void*, int, int)>(original, 0x004A0CF0)(viewport, nullptr,
                                                                                                  1, 2) & 0xFF});
                    Record({At<uint32_t(__thiscall*)(void*, float, float, float, float, int, int)>(original,
                                                                                                 0x004A0CE0)(
                                viewport, 1, 2, 3, 4, 5, 6) & 0xFF});
                    Record({At<uint32_t(__thiscall*)(void*, int)>(original, 0x004A0D00)(viewport, 7)});
                    break;
                }
                case 11:
                    At<void(__thiscall*)(void*)>(original, 0x004A0E60)(viewport);
                    NewViewport(original, random, camera); // (a new one, some cameras' lists longer)
                    break;
                default:
                    Record({Relocate(At<uint32_t(__thiscall*)(void*, int)>(original, 0x004A0E80)(
                        viewport, static_cast<int>(random.Below(4))))});
                    (&GameVariable<uint8_t*>(0x00701350))[random.Below(10)] = NewViewport(original, random, camera);
                    break;
                }
                memory.Dump(g_trace);
            }
            std::vector<uint32_t> trace = g_trace;
            DumpArena(trace);
            return trace;
        });
    }
    // The ten viewports made.
    ok &= CompareTwoRuns("viewports made", reported, [&](bool original) {
        g_trace.clear();
        ResetArena(0xEE);
        memory.Fill(0x7E4);
        At<void(__cdecl*)()>(original, 0x004A0840)();
        std::vector<uint32_t> trace = g_trace;
        memory.Dump(trace);
        return trace;
    });
    delete &s;
    g_relocate = nullptr;
    Check(ok, "viewports: made, cameras set / marked / deleted, modes, field of view, closest camera, stubs, destroyed");
}

// The occlusion requests: requested (slots taken, the list full), issued (callbacks), popped.
void TestOcclusionRequests()
{
    GameMemory memory(kMemory);
    StandIns& s = *new StandIns();
    StandInViewportCalls(s);
    auto* callback = s.Thunk(0, [](Regs& r) {
        Record({0xCA11, r.args[0], r.args[1]});
        return 0u;
    });
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 40; ++round) {
        ok &= CompareTwoRuns("occlusion requests", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x0CC + round);
            Lcg random(0x0CC0 + round);
            auto* slots = &GameVariable<uint8_t>(0x0067E550);
            for (int i = 0; i < 0x400; ++i)
                slots[i] = random.Below(round % 4 == 3 ? 1000 : 3) ? 1 : 0;
            GameVariable<uint32_t>(0x00701380) = random.Below(4);
            for (int i = 0; i < 4; ++i) { // (the callbacks: ours or none)
                auto& request = (&GameVariable<Nx::OcclusionRequest>(0x00701390))[i];
                request.callback = random.Below(3) ? reinterpret_cast<void(__cdecl*)(uint32_t, uint32_t)>(callback)
                                                   : nullptr;
                if (random.Below(4) == 0)
                    request.slot = 0;
                request.issued = static_cast<uint8_t>(random.Below(3) ? 0 : random.Next());
            }
            for (int step = 0; step < 20; ++step) {
                switch (random.Below(3)) {
                case 0: {
                    Nx::OcclusionRequest request = {};
                    request.slot = random.Next();
                    if (random.Below(4))
                        request.callback = reinterpret_cast<void(__cdecl*)(uint32_t, uint32_t)>(callback);
                    request.data = random.Next();
                    request.issued = static_cast<uint8_t>(random.Below(2));
                    request.pad[0] = 9;
                    if (random.Below(4) == 0)
                        request.slot = 0;
                    Record({At<uint32_t(__cdecl*)(void*)>(original, 0x004A0EB0)(&request), request.slot});
                    break;
                }
                case 1:
                    At<void(__cdecl*)()>(original, 0x004A0F30)();
                    break;
                default:
                    At<void(__cdecl*)()>(original, 0x004A0F80)();
                    break;
                }
                memory.Dump(g_trace);
            }
            return g_trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "occlusion requests: slots taken, a full list, callbacks issued once, the first dropped");
}

// The script commands: Set2DIn3DSpaceParams, ScriptGetScreenMode, SetScreenMode.
void TestScreenModeCommands()
{
    GameMemory memory(kMemory);
    StandIns& s = *new StandIns();
    StandInViewportCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    const uint32_t names[] = {0x6F70EA65, 0x9B62E5A6, 0xB07B580E, 0xB6624A73, 0x6F2D1231, 0xF624438B, 0x12345678, 0};
    for (int round = 0; round < 40; ++round) {
        ok &= CompareTwoRuns("screen mode commands", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x5C5 + round);
            Lcg random(0x5C50 + round);
            for (int i = 0; i < 10; ++i)
                (&GameVariable<uint8_t*>(0x00701350))[i] = NewViewport(original, random, nullptr);
            auto* params = reinterpret_cast<Script::CStruct*>(Bump(0x10));
            auto* script = reinterpret_cast<Script::CScript*>(Bump(0x200));
            script->m_params = reinterpret_cast<Script::CStruct*>(Bump(0x10));
            g_answers.clear();
            for (int i = 0; i < 3; ++i)
                g_answers.insert(g_answers.end(), {random.Below(2), random.Next()});
            Record({At<uint32_t(__cdecl*)(void*, void*)>(original, 0x004A07D0)(params, script) & 0xFF});
            GameVariable<int>(0x0070134C) = static_cast<int>(random.Below(8)) - (random.Below(5) == 0 ? 3 : 0);
            Record({At<uint32_t(__cdecl*)(void*, void*)>(original, 0x004A08A0)(params, script) & 0xFF});
            g_answers = {1, names[random.Below(8)]};
            Record({At<uint32_t(__cdecl*)(void*, void*)>(original, 0x004A0C20)(params, script) & 0xFF});
            memory.Dump(g_trace);
            return g_trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "screen mode commands: 2D-in-3D parameters, the mode's name, a mode set by name");
}

} // namespace

SELFTEST(Early, TestViewports, RemoveStandIns);
SELFTEST(Early, TestOcclusionRequests, RemoveStandIns);
SELFTEST(Early, TestScreenModeCommands, RemoveStandIns);
