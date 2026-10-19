#pragma once

// Shared pieces of the animation range's self-tests (selftests_anim*.cpp): the appmain kit (our function at a game
// address, recording stand-ins, game memory, Lcg) and a trace comparison that logs the first differences.

#include <cstdint>
#include <vector>

#include "hook/appmain_testkit.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace AnimTests {

using namespace AppMainTests;

inline void CompareTraces(const std::vector<uint32_t>& expected, const std::vector<uint32_t>& actual, const char* what)
{
    Check(expected == actual, what);
    if (expected == actual)
        return;
    if (expected.size() != actual.size())
        Log("  trace sizes: original %u ours %u", static_cast<unsigned>(expected.size()),
            static_cast<unsigned>(actual.size()));
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 10; ++i) {
        if (expected[i] != actual[i]) {
            if (!shown) { // (what led up to the first difference)
                for (size_t k = i > 12 ? i - 12 : 0; k < i; ++k)
                    Log("  entry %u: %08X", static_cast<unsigned>(k), expected[k]);
            }
            Log("  entry %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
}

// The asset vtables: the base's and each type's (22 slots).
inline const uint32_t kAssetVtables[] = {0x00645568, 0x006454B0, 0x00645600, 0x00645698, 0x00645700,
                                         0x00645778, 0x00645810, 0x00645888, 0x00645928, 0x006459A8};

// For our run: every slot of the asset vtables (and the asset manager's and the task lists' few) that holds a
// function we replaced points at ours, as with the hooks installed. (Written for the run only: EndRunPatches.)
inline void UseOurVtables()
{
    for (uint32_t vtable : kAssetVtables) {
        for (int slot = 0; slot < 22; ++slot) {
            uint32_t target = reinterpret_cast<uint32_t*>(vtable)[slot];
            if (void* ours = OursAt(target))
                RunPatch(vtable + 4 * slot, Dword(Resolve(ours)));
        }
    }
    for (uint32_t slot : {0x00645658u, 0x0064565Cu, 0x00645660u, 0x006454A4u, 0x006454A8u, 0x0064549Cu}) {
        if (void* ours = OursAt(*reinterpret_cast<uint32_t*>(slot)))
            RunPatch(slot, Dword(Resolve(ours)));
    }
}

} // namespace AnimTests
