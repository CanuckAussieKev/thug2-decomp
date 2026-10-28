// Self-tests for the engine's texture, the folded slot dispatchers (src/game/gfx/engine_texture.cpp) and the shared
// stubs 0x0049F430-0x0049F4C0 (src/game/core/stubs.cpp), RND4-3: the original and ours over stand-in textures with
// a recording vtable (the game's sprintf stood in), every result, call and the arena compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/gfx/engine_texture.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::CTexture;

void* g_vtable[48];
uint32_t g_calls;

uint8_t* Block(uint32_t size) { return reinterpret_cast<uint8_t*>(Bump(size)); }

// The bytes each slot pops (its arguments): what the dispatchers' tests call them with.
uint16_t SlotPop(int slot)
{
    switch (slot) {
    case 0: return 4;
    case 1: return 12;
    case 2: return 20;
    case 3: return 24;
    case 17: return 12;
    case 21: return 12;
    case 23: return 8;
    case 24: return 16;
    case 32: return 4;
    case 41: return 8;
    default: return 0;
    }
}

void StandInTexture(StandIns& s)
{
    StandInMemory(s);
    s.At({0x006234FE}, 0, [](Regs& r) { return StandInSprintf(r); });
    for (int slot = 0; slot < 48; ++slot) {
        uint16_t pop = SlotPop(slot);
        g_vtable[slot] = s.Thunk(pop, [slot, pop](Regs& r) {
            Record({0xA000u + slot, Relocate(r.ecx)});
            for (int i = 0; i < pop / 4; ++i)
                Record({Relocate(r.args[i])});
            if (slot == 1) // the path it's given
                Record({Hash(reinterpret_cast<const char*>(r.args[0]))});
            return 0xC0DE0000u + (slot << 8) + (++g_calls & 0xFF);
        });
    }
}

template <typename F>
F At(bool original, uint32_t address)
{
    return Fn<F>(original, address);
}

void TestEngineTexture()
{
    GameMemory memory({{0x007CC979, 1}});
    StandIns& s = *new StandIns();
    StandInTexture(s);
    g_relocate = ArenaRelocate;
    const char* names[] = {"foo.png", "a/b\\c.png", "../x/y.png", "./z", "", "dir/", "\\lead", "..x", "../", ".",
                           "images/sub/t.img", "a//b"};
    bool ok = true, reported = false;
    for (int round = 0; round < 30; ++round) {
        ok &= CompareTwoRuns("engine texture", reported, [&](bool original) {
            g_trace.clear();
            g_calls = 0;
            ResetArena(0xEE);
            GameVariable<bool>(0x007CC979) = true;
            Lcg random(0x7E70 + round);
            auto result = [&](uint32_t value) { Record({0xEE00, Relocate(value)}); };
            auto* texture = reinterpret_cast<CTexture*>(Block(0x14));
            for (int i = 0; i < 0x14; ++i)
                reinterpret_cast<uint8_t*>(texture)[i] = static_cast<uint8_t>(random.Next());
            result(At<uint32_t(__thiscall*)(void*)>(original, 0x0049F200)(texture));
            for (int step = 0; step < 16; ++step) {
                texture->vtable = g_vtable;
                uint32_t word = random.Next() ^ (random.Next() << 10);
                switch (random.Below(7)) {
                case 0:
                case 1: {
                    uint32_t images = random.Pick({0u, 1u, 0x100u, 0xFFFFFF01u, 2u});
                    result(At<uint32_t(__thiscall*)(void*, const char*, uint32_t, uint32_t)>(original, 0x0049F220)(
                        texture, names[random.Below(sizeof(names) / sizeof(names[0]))], images, word));
                    break;
                }
                case 2:
                    result(At<uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                                    uint32_t)>(original, 0x0049F300)(texture, 1, 2, word, 4, 5, 6));
                    break;
                case 3:
                    result(At<uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                                    uint32_t)>(original, 0x0049F330)(texture, word, 7, 8, 9, 10, 11));
                    break;
                case 4:
                    At<void(__thiscall*)(void*, uint32_t)>(original, 0x0049F3F0)(texture, random.Pick({0u, 1u, 0x100u,
                                                                                                    0xFFFFFF01u}));
                    break;
                case 5:
                    texture->m_flags = word;
                    result(At<uint32_t(__thiscall*)(void*)>(original, 0x0049F200)(texture));
                    break;
                default:
                    if (random.Below(2)) {
                        At<void(__thiscall*)(void*)>(original, 0x0049F210)(texture);
                    } else {
                        result(At<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0049F4D0)(texture,
                                                                                             random.Below(4)));
                    }
                    result(reinterpret_cast<uint32_t>(texture->vtable));
                    break;
                }
                Record({texture->m_checksum, texture->m_flags});
            }
            std::vector<uint32_t> trace = g_trace;
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "engine texture: construct, load by name (paths, images/, ../), from memory, create, flags");
}

// Calling a dispatcher (a tail jump) with `this` and `count` arguments: what the slot returns, and how far the
// stack moved (0 when the callee popped its arguments).
uint32_t CallDispatcher(void* code, void* object, const uint32_t* args, int count, uint32_t* moved)
{
    uint32_t result, before, after;
    __asm {
        mov before, esp
        mov ecx, count
        mov edx, args
    push_loop:
        test ecx, ecx
        jz pushed
        dec ecx
        push dword ptr [edx + ecx * 4]
        jmp push_loop
    pushed:
        mov ecx, object
        call code
        mov result, eax
        mov after, esp
        mov esp, before
    }
    *moved = before - after;
    return result;
}

void TestSlotDispatchers()
{
    StandIns& s = *new StandIns();
    StandInTexture(s);
    g_relocate = ArenaRelocate;
    const struct {
        uint32_t address;
        int slot;
    } dispatchers[] = {{0x0049F360, 17}, {0x0049F370, 21}, {0x0049F380, 23}, {0x0049F390, 24}, {0x0049F3A0, 26},
                       {0x0049F3B0, 32}, {0x0049F3C0, 33}, {0x0049F3D0, 34}, {0x0049F3E0, 38}, {0x0049F420, 41}};
    // the shared stubs: their argument counts
    const struct {
        uint32_t address;
        int args;
        uint32_t mask; // what the result is: AL, AX or EAX
    } stubs[] = {{0x0049F430, 7, 0xFF}, {0x0049F440, 5, 0xFF}, {0x0049F450, 4, 0xFF},     {0x0049F460, 0, 0xFFFF},
                 {0x0049F470, 2, 0xFF}, {0x0049F480, 3, ~0u},  {0x0049F490, 6, ~0u},      {0x0049F4A0, 1, ~0u},
                 {0x0049F4B0, 1, 0xFF}, {0x0049F4C0, 2, ~0u}};
    bool reported = false;
    bool ok = CompareTwoRuns("slot dispatchers", reported, [&](bool original) {
        g_trace.clear();
        g_calls = 0;
        ResetArena(0xEE);
        auto** object = reinterpret_cast<void***>(Block(0x10));
        *object = g_vtable;
        const uint32_t args[8] = {0x11111111, 0x22222222, 0x3F800000, 0x44444444, 5, 6, 7, 8};
        for (auto d : dispatchers) {
            void* code = Code(original, d.address);
            uint32_t moved;
            uint32_t result = CallDispatcher(code, object, args, SlotPop(d.slot) / 4, &moved);
            Record({d.address, result, moved});
        }
        for (auto st : stubs) {
            void* code = Code(original, st.address);
            uint32_t moved;
            uint32_t result = CallDispatcher(code, object, args, st.args, &moved);
            Record({st.address, result & st.mask, moved}); // (above AL / AX: whatever EAX held)
        }
        std::vector<uint32_t> trace = g_trace;
        DumpArena(trace);
        return trace;
    });
    delete &s;
    g_relocate = nullptr;
    Check(ok, "the folded slot dispatchers and the shared constant stubs (arguments, results, stack)");
}

} // namespace

SELFTEST(Early, TestEngineTexture, RemoveStandIns);
SELFTEST(Early, TestSlotDispatchers, RemoveStandIns);
