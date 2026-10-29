// Self-tests for the model appearance (src/game/gfx/model_appearance.cpp): original and ours on appearances in an
// arena, the structures, CAS option lists, face textures, CObject's code and the buffer reads / writes stood in by
// recording code; random sequences of every member, the calls, results and memory compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/model_appearance.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/script/struct_buffer.h"
#include "hook/cas4_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace Cas4Tests;
using Gfx::CModelAppearance;

constexpr uint32_t kParts[] = {0x650FAB6D, 0x2457F44D, 0x3FA9B96E, 0x609432CA, 0x0000B0A1};
constexpr uint32_t kNames[] = {0xB6F08F39, 0x4BB2084E, 0x2E0B1465, 0xA40ABAA7, 0xE288A7CB,
                               0x97DBDDE6, 0x6E94F918, 0xE4F130F4, 0x949BC47B};
constexpr uint32_t kMembers[] = {0x83339D0B, 0x10A225D6, 0x0A23400C, 0xB13906B0, 0xE961BAFA, 0xD27427FF, 0x6C63C7C5};

Lcg* g_rand;            // the stand-ins' answers
uint32_t g_options[4];  // CAS options handed out (0: none)

void StandInAppearanceCalls(StandIns& s)
{
    StandInMemory(s);
    StandInStructs(s);
    StandInStructPool(s);
    using CallMember = bool (Obj::CObject::*)(uint32_t, Script::CStruct*, Script::CScript*);
    s.At({0x00460B80, AddressOf(&Obj::CObject::Construct)}, 0, [](Regs& r) {
        Record({0x0B80, Relocate(r.ecx)});
        *reinterpret_cast<uint32_t*>(r.ecx) = 0x006477E8;
        return r.ecx;
    });
    s.At({0x004615D0, AddressOf(&Obj::CObject::DestroyBody)}, 0, [](Regs& r) {
        Record({0x15D0, Relocate(r.ecx), *reinterpret_cast<uint32_t*>(r.ecx)});
        return 0u;
    });
    s.At({0x004608F0, AddressOf(static_cast<CallMember>(&Obj::CObject::CallMemberFunction))}, 12, [](Regs& r) {
        Record({0x08F0, Relocate(r.ecx), r.args[0], Relocate(r.args[1]), Relocate(r.args[2])});
        return r.args[0] & 1;
    });
    s.At({0x00485CF0}, 0, [](Regs& r) {
        Record({0x5CF0, r.args[0], r.args[1], r.args[2] & 0xFF});
        return g_options[(r.args[0] ^ r.args[1]) & 3];
    });
    s.At({0x00485E20}, 0, [](Regs& r) {
        Record({0x5E20, r.args[0], r.args[1]});
        return g_options[(r.args[0] + r.args[1]) & 3];
    });
    s.At({0x00485FC0}, 0, [](Regs& r) {
        Record({0x5FC0, r.args[0], r.args[1], r.args[2]});
        return g_options[(r.args[1] * 3 + r.args[2]) & 3];
    });
    s.At({0x00487510}, 0, [](Regs& r) {
        Record({0x7510, Relocate(r.ecx)});
        return static_cast<uint32_t>(*reinterpret_cast<uint8_t*>(r.ecx));
    });
    s.At({0x00487520}, 4, [](Regs& r) {
        Record({0x7520, Relocate(r.ecx), r.args[0] & 0xFF});
        *reinterpret_cast<uint8_t*>(r.ecx) = static_cast<uint8_t>(r.args[0]);
        return 0u;
    });
    s.At({0x004875C0}, 0, [](Regs& r) {
        Record({0x75C0, Relocate(r.ecx)});
        *reinterpret_cast<uint8_t*>(r.ecx) = 0;
        reinterpret_cast<uint8_t*>(r.ecx)[0x445C] = 0x5A;
        return r.ecx;
    });
    s.At({0x00479E20, AddressOf(&Script::WriteToBuffer)}, 0, [](Regs& r) {
        Record({0x9E20, Relocate(r.args[0]), Relocate(r.args[1]), r.args[2], r.args[3], r.args[4]});
        static const uint32_t kSizes[] = {0x10, 0x2BC, 0x2BD, 0x300, 0x80000010};
        uint32_t bytes = kSizes[g_rand->Below(5)];
        if (bytes > 0x400)
            bytes = 0x20;
        reinterpret_cast<uint8_t*>(r.args[1])[bytes] = 0xEE;
        return bytes;
    });
    s.At({0x0047A0C0, AddressOf(&Script::ReadFromBuffer)}, 0, [](Regs& r) {
        Record({0xA0C0, Relocate(r.args[0]), Relocate(r.args[1])});
        return r.args[1] + g_rand->Below(8);
    });
}

// A stand-in structure (8 bytes in the arena) with random values among `kNames` / the parts.
uint32_t MakeStruct(Lcg& random, bool full)
{
    uint32_t s = Bump(8);
    for (uint32_t name : kNames) {
        if (full || random.Below(3))
            SetValue(reinterpret_cast<void*>(s), name, random.Below(3) ? random.Pick(kParts) : random.Next());
    }
    if (random.Below(2))
        SetValue(reinterpret_cast<void*>(s), 0x97DBDDE6, random.Below(2));
    // (with use_default_hsv 0, a missing s or v is the original's stack garbage: always given then)
    auto it = g_values.find({s, 0x97DBDDE6});
    if (it != g_values.end() && it->second == 0) {
        for (uint32_t name : {0x6E94F918u, 0xE4F130F4u, 0x949BC47Bu})
            SetValue(reinterpret_cast<void*>(s), name, random.Next() & 0xFF);
        if (random.Below(3) == 0) // (a missing h: the parameter's own stack slot, `params`)
            g_values.erase({s, 0x6E94F918u});
    }
    return s;
}

// Random appearances driven through every member (both runs alike); each result recorded.
void TestModelAppearance()
{
    GameMemory& memory = *new GameMemory({{0x007CC979, 1}, {0x007D1800, 4}, {0x006F3614, 16}});
    StandIns& s = *new StandIns();
    StandInAppearanceCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 60; ++round) {
        ok &= CompareRuns("model appearance", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            ClearStructs();
            memory.Zero();
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<uint32_t>(0x007D1800) = 0x3A9A0000;
            UseStructPool(round & 1);
            Lcg random(0x4A00 + round);
            Lcg answers(0x9100 + round);
            g_rand = &answers;
            std::vector<uint32_t> trace;
            CModelAppearance* apps[2];
            for (CModelAppearance*& app : apps) {
                app = reinterpret_cast<CModelAppearance*>(Bump(sizeof(CModelAppearance)));
                trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x00488EA0)(app)));
            }
            // the CAS options (random_set, desc_id, flags)
            for (int i = 0; i < 4; ++i) {
                g_options[i] = i == 0 && round % 3 == 0 ? 0 : MakeStruct(random, false);
                if (g_options[i]) {
                    SetValue(reinterpret_cast<void*>(g_options[i]), 0x0D7260FD, random.Next());
                    if (random.Below(2))
                        g_flags.insert({g_options[i], random.Pick(kParts)});
                }
            }
            g_globalStructs[0xFE54486D] = round % 4 == 1 ? 0 : MakeStruct(random, false);
            uint32_t named = MakeStruct(random, false);
            g_globalStructs[0x1234] = named;
            // the slots each appearance's structure holds
            for (CModelAppearance* app : apps) {
                for (uint32_t part : kParts) {
                    if (random.Below(3))
                        SetValue(&app->m_structure, part, MakeStruct(random, false));
                }
                // the components ResolveRandomParts walks
                std::vector<uint32_t>& list = g_components[Ptr(&app->m_structure)];
                int count = static_cast<int>(random.Below(6));
                for (int i = 0; i < count; ++i) {
                    auto* component = reinterpret_cast<uint32_t*>(Bump(12));
                    uint32_t type = random.Below(3) ? Script::ESYMBOLTYPE_STRUCTURE : random.Below(14);
                    component[0] = (type << 1) | random.Below(2) | 0xAB00;
                    component[1] = random.Pick(kParts);
                    component[2] = MakeStruct(random, false);
                    if (random.Below(2))
                        g_flags.insert({component[2], 0xD81C03B0});
                    if (random.Below(2))
                        SetValue(reinterpret_cast<void*>(component[2]), 0x4B833E64, random.Below(5));
                    list.push_back(Ptr(component));
                }
            }
            auto* script = reinterpret_cast<uint8_t*>(Bump(0x100));
            *reinterpret_cast<uint32_t*>(script + 0x14) = Bump(8);
            auto* buffer = reinterpret_cast<uint8_t*>(Bump(0x420));
            for (int step = 0; step < 40; ++step) {
                CModelAppearance* app = apps[random.Below(2)];
                CModelAppearance* other = apps[random.Below(2)];
                uint32_t op = random.Below(17);
                trace.push_back(0xC0DE0000 + op);
                switch (op) {
                case 0:
                    trace.push_back(Fn<uint8_t(__thiscall*)(void*)>(original, 0x00488F00)(app));
                    break;
                case 1: {
                    uint32_t from = MakeStruct(random, false);
                    trace.push_back(Fn<uint8_t(__thiscall*)(void*, uint32_t, bool)>(original, 0x00488F40)(
                        app, from, random.Below(2) != 0));
                    break;
                }
                case 2:
                    trace.push_back(Fn<uint8_t(__thiscall*)(void*, uint32_t, bool)>(original, 0x00488FA0)(
                        app, 0x1234, random.Below(2) != 0));
                    break;
                case 3:
                    Fn<void(__thiscall*)(void*)>(original, 0x00488DB0)(app);
                    break;
                case 4:
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x00489050)(
                        app, random.Pick(kParts))));
                    break;
                case 5:
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x004890B0)(
                        app, random.Pick(kParts))));
                    break;
                case 6: {
                    uint32_t params = MakeStruct(random, random.Below(2) != 0);
                    Fn<void(__thiscall*)(void*, uint32_t, uint32_t, uint32_t)>(original, 0x004891F0)(
                        app, random.Pick(kParts), random.Next(), params);
                    break;
                }
                case 7: {
                    uint32_t params = MakeStruct(random, random.Below(3) == 0);
                    trace.push_back(Fn<uint8_t(__thiscall*)(void*, uint32_t, uint32_t, void*)>(original, 0x004894D0)(
                        app, random.Pick(kMembers), params, script));
                    break;
                }
                case 8:
                    if (round % 5 == 3 && random.Below(2))
                        g_failAllocations.push_back(true);
                    Fn<void(__thiscall*)(void*)>(original, 0x00489160)(app);
                    g_failAllocations.clear();
                    if (app->mp_faceTexture) {
                        app->mp_faceTexture->m_valid = random.Below(2) != 0;
                        uint32_t seed = random.Next(); // (its picture: copies must take all of it)
                        uint8_t* bytes = reinterpret_cast<uint8_t*>(app->mp_faceTexture);
                        for (size_t i = 1; i < sizeof(Gfx::CFaceTexture); ++i)
                            bytes[i] = static_cast<uint8_t>((seed = seed * 1103515245u + 12345u) >> 16);
                    }
                    break;
                case 9:
                    Fn<void(__thiscall*)(void*)>(original, 0x004891D0)(app);
                    break;
                case 10:
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, void*)>(original, 0x004893A0)(
                        app, random.Below(4) ? other : app)));
                    break;
                case 11:
                    trace.push_back(Fn<int(__thiscall*)(void*, uint8_t*, int, bool)>(original, 0x004896A0)(
                        app, buffer, 0x400 + static_cast<int>(random.Below(16)), random.Below(3) == 0));
                    break;
                case 12:
                    for (int i = 0; i < 8; ++i)
                        buffer[i] = random.Below(3) ? 0 : static_cast<uint8_t>(random.Next());
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, uint8_t*)>(original, 0x00489400)(
                        app, buffer)));
                    break;
                case 13: {
                    uint32_t target = MakeStruct(random, false);
                    Fn<void(__cdecl*)(uint32_t)>(original, 0x00488FD0)(target);
                    break;
                }
                case 14: // (source only: ours called directly)
                    app->m_hasFaceTexture = random.Below(2) != 0;
                    trace.push_back(original ? Original<uint8_t(__thiscall*)(void*)>(0x004890E0)(app)
                                             : app->HasFaceTexture());
                    break;
                case 15:
                    app->m_hasFaceTexture = random.Below(2) != 0;
                    break;
                default: { // destroyed, and made again
                    if (random.Below(2)) {
                        Fn<void(__thiscall*)(void*)>(original, 0x004890F0)(app);
                    } else {
                        trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned int)>(
                            original, 0x00489370)(app, random.Below(2) | 2u)));
                    }
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x00488EA0)(app)));
                    break;
                }
                }
                trace.insert(trace.end(), g_trace.begin(), g_trace.end());
                g_trace.clear();
            }
            DumpArena(trace);
            memory.Dump(trace);
            return trace;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "model appearance: made, reset, loaded, resolved, parts set / read / cleared, member functions, face "
              "textures, copied, written to and read from buffers, destroyed");
}

} // namespace

SELFTEST(Early, TestModelAppearance, RemoveStandIns);
