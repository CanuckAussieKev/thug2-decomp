#pragma once

// Shared pieces of the renderer's last part's self-tests (selftests_rnd5_*.cpp): the script structure's getters and
// AddChecksum stood in with scripted answers, Dbg::Printf recorded, random floats.

#include <windows.h>

#include <cstdint>
#include <limits>
#include <vector>

#include "game/core/dbg.h"
#include "game/script/struct.h"
#include "hook/rfx_testkit.h"

namespace Rnd5Tests {

using namespace RfxTests;

template <typename F>
F At(bool original, uint32_t address)
{
    return Fn<F>(original, address);
}

inline float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}
inline float AwkwardFloat(Lcg& random, float low, float high)
{
    switch (random.Below(8)) {
    case 0:
        return 0.0f;
    case 1:
        return -0.0f;
    case 2:
        return std::numeric_limits<float>::quiet_NaN();
    default:
        return RandomFloat(random, low, high);
    }
}

// The script structure's answers, scripted per call (pairs: found, value).
inline std::vector<uint32_t> g_answers;
inline bool NextAnswer(uint32_t* value)
{
    if (g_answers.size() < 2)
        return false;
    bool found = g_answers[0] != 0;
    if (found)
        *value = g_answers[1];
    g_answers.erase(g_answers.begin(), g_answers.begin() + 2);
    return found;
}

// The getters record {tag, the structure, the name (a string's hash), assert} and answer from g_answers; AddChecksum
// records; Dbg::Printf records its format's hash.
inline void StandInScriptStructure(StandIns& s)
{
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0x1C30, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0xFFFFFFFFu;
    });
    auto getter = [](uint32_t tag, bool named) {
        return [tag, named](Regs& r) {
            Record({tag, Relocate(r.ecx), named ? Hash(reinterpret_cast<const char*>(r.args[0])) : r.args[0],
                    r.args[2] & 0xFF});
            uint32_t value;
            if (!NextAnswer(&value))
                return 0u;
            *reinterpret_cast<uint32_t*>(r.args[1]) = value;
            return 1u;
        };
    };
    using Script::CStruct;
    s.At({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 12,
         getter(0x66D0, false));
    s.At({0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         getter(0x6630, false));
    s.At({0x00476680, AddressOf(static_cast<bool (CStruct::*)(const char*, int*, int)>(&CStruct::GetInteger))}, 12,
         getter(0x6680, true));
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))},
         12, getter(0x6950, false));
    s.At({0x00476EC0}, 12, getter(0x6950, false)); // (the bool-assert wrapper: the same call)
    s.At({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 8,
         [](Regs& r) {
             Record({0x7D40, Relocate(r.ecx), r.args[0], r.args[1]});
             return 0u;
         });
}

// A fake vtable of `slots` recording slots: {0x5100 + slot, this, its `args[slot]` stack arguments}; returns this.
struct RecordingVtable {
    uint32_t slots[16];
    void Make(StandIns& s, std::initializer_list<int> args)
    {
        int slot = 0;
        for (int count : args) {
            slots[slot] = reinterpret_cast<uint32_t>(s.Thunk(static_cast<uint16_t>(4 * count), [slot, count](Regs& r) {
                Record({0x5100u + slot, Relocate(r.ecx)});
                for (int i = 0; i < count; ++i)
                    Record({Relocate(r.args[i])});
                return r.ecx;
            }));
            ++slot;
        }
    }
};

} // namespace Rnd5Tests
