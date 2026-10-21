#pragma once

// The self-tests' THUGPRO big-level positions (TP-R2, game/net/thugpro_compat.h): the original with THUGPRO's own
// patches written in (its THUGPRO_SetBigLevel: the scaling instructions NOPped) against ours with the same scale set,
// so what ours sends and reads in a big level is proven to be what THUGPRO's does.

#include <cstdint>
#include <vector>

#include "game/net/thugpro_compat.h"
#include "hook/standins.h"

namespace ThugproTests {

// The scale modes tested: 0 THUG2's, 1 big (x / z whole), 2 tall (y whole), 3 both.
constexpr int kScaleModes = 4;
inline GameNet::Thugpro::PositionScale ScaleOf(int mode) { return {(mode & 1) != 0, (mode & 2) != 0}; }

// THUGPRO's NOPs (its cfuncs.cpp THUGPRO_SetBigLevel 1, without / with Height): the x / z doubling or halving and
// the y quartering. `firstEntry`: also the client reader's first entry (0x0056E470's own copy of the skater's
// position, which THUGPRO left scaled - ours writes it as sent, our one deviation from THUGPRO there).
inline void WriteThugproScalePatches(int mode, bool firstEntry)
{
    struct Site {
        uint32_t address;
        uint32_t size;
        bool height;
    };
    static const Site sites[] = {
        {0x0056C1E4, 6, false}, {0x0056C20C, 6, false}, {0x0056E7FB, 6, false}, {0x0056E824, 6, false},
        {0x0051306E, 2, false}, {0x0051345F, 2, false}, {0x00569775, 2, false}, {0x005699BD, 2, false},
        {0x0056C1FF, 6, true},  {0x0056E813, 6, true},  {0x00513058, 6, true},  {0x0051343B, 6, true},
        {0x0056975F, 6, true},  {0x0056999A, 6, true},
    };
    GameNet::Thugpro::PositionScale scale = ScaleOf(mode);
    for (const Site& site : sites) {
        if (site.height ? scale.tall : scale.big)
            TestStandIns::RunPatch(site.address, std::vector<uint8_t>(site.size, 0x90));
    }
    if (firstEntry && scale.big)
        TestStandIns::RunPatch(0x0056E725, std::vector<uint8_t>(2, 0x90));
    if (firstEntry && scale.tall)
        TestStandIns::RunPatch(0x0056E70C, std::vector<uint8_t>(6, 0x90));
}

// One run's scale: the original's patches (EndRunPatches takes them out) or ours set.
inline void UseScale(bool original, int mode, bool firstEntry = false)
{
    if (original)
        WriteThugproScalePatches(mode, firstEntry);
    else
        GameNet::Thugpro::SetPositionScale(ScaleOf(mode));
}
inline void EndScale()
{
    TestStandIns::EndRunPatches();
    GameNet::Thugpro::SetPositionScale({});
}

} // namespace ThugproTests
