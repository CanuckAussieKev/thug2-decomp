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
            Log("  entry %u: original %08X ours %08X", static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    }
}

} // namespace AnimTests
