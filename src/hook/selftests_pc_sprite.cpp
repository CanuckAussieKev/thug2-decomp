// Self-tests for the PC sprite (src/game/gfx/pc_sprite.cpp, RFX-14): the original and ours over stand-in sprites,
// the engine's constructor / destructor and the 2D sprite's code stood in.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/gfx/draw2d.h"
#include "game/gfx/pc_sprite.h"
#include "game/gfx/sprite.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

void* g_spriteVtable[1];

void TestPcSprites()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}});
    StandIns& s = *new StandIns();
    StandInMemory(s);
    s.At({0x0049ED60}, 4, [](Regs& r) {
        Record({0xED60, Relocate(r.ecx), r.args[0]});
        return r.ecx;
    });
    StandInThis(s, {0x0049EBA0}, 0);
    s.At({0x004DFC50, AddressOf(&Nx::SSprite::Construct)}, 4, [](Regs& r) {
        Record({0xFC50, Relocate(r.ecx), r.args[0]});
        *reinterpret_cast<void***>(r.ecx) = g_spriteVtable;
        return r.ecx;
    });
    g_spriteVtable[0] = s.Thunk(4, [](Regs& r) {
        Record({0xFCC0, Relocate(r.ecx), r.args[0]});
        return r.ecx;
    });
    s.At({0x004DFB70, AddressOf(&Nx::SDraw2D::SetHidden)}, 4, [](Regs& r) {
        Record({0xFB70, Relocate(r.ecx), r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x004DFAE0, AddressOf(&Nx::SDraw2D::SetPriority)}, 4, [](Regs& r) {
        Record({0xFAE0, Relocate(r.ecx), r.args[0]});
        return 0u;
    });
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 12; ++round) {
        ok &= CompareTwoRuns("pc sprites", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x5B0 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            Lcg random(0x5B00 + round);
            std::vector<uint32_t> trace;
            auto* sprite = reinterpret_cast<Nx::CPcSprite*>(Bump(sizeof(Nx::CPcSprite)));
            if (round == 3)
                g_failAllocations = {true};
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004C70E0)(sprite)));
            if (sprite->sprite) {
                auto* bytes = reinterpret_cast<uint8_t*>(sprite);
                for (int i = 4; i < 0x44; ++i)
                    bytes[i] = static_cast<uint8_t>(random.Next());
                sprite->x = RandomFloat(random, -100, 700);
                sprite->y = RandomFloat(random, -100, 500);
                sprite->xscale = RandomFloat(random, -2, 2);
                sprite->yscale = RandomFloat(random, -2, 2);
                sprite->hotX = RandomFloat(random, -1, 1);
                sprite->hotY = RandomFloat(random, -1, 1);
                sprite->rotation = RandomFloat(random, -4, 4);
                sprite->priority = RandomFloat(random, -10, 10);
                sprite->width = static_cast<uint16_t>(random.Below(3000));
                sprite->height = static_cast<uint16_t>(random.Below(3000));
                if (round % 3 == 0) {
                    sprite->texture = nullptr;
                } else {
                    uint8_t* holder = reinterpret_cast<uint8_t*>(Bump(0x20));
                    uint32_t key = random.Next();
                    if (round % 3 == 2) {
                        auto* pointer = reinterpret_cast<uint32_t*>(Bump(4));
                        *pointer = key;
                        *reinterpret_cast<uint32_t**>(holder + 0x10) = pointer;
                        holder[8] = 1;
                    } else {
                        *reinterpret_cast<uint32_t*>(holder + 0x10) = key;
                        holder[8] = 2;
                    }
                    sprite->texture = holder;
                }
                Fn<void(__thiscall*)(void*)>(original, 0x004C71B0)(sprite);
                Fn<void(__thiscall*)(void*)>(original, 0x004C70B0)(sprite);
                Fn<void(__thiscall*)(void*)>(original, 0x004C70C0)(sprite);
                trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(sprite->sprite), 0x40));
            } else {
                Fn<void(__thiscall*)(void*)>(original, 0x004C70C0)(sprite);
            }
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x004C7180)(
                sprite, round & 1)));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "pc sprites: made (no memory once), the 2D sprite updated, hidden, its priority, destroyed");
}

} // namespace

SELFTEST(Early, TestPcSprites, RemoveStandIns);
