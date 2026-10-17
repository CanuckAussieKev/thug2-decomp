#pragma once

// Mdl::CCompetition (the skate module's competition, Mdl::Skate +0x360): only the pieces the linker placed after
// the front end (0x0054FFD0-0x0055002F) have source so far.

#include <cstddef>
#include <cstdint>

#include "game/core/string.h"

namespace Script {
class CStruct;
} // namespace Script

namespace Mdl {

struct CCompetition {
    // One of the eight standings (0x20 bytes): a name, then what isn't known yet.
    struct Leader {
        Str::String m_name;
        uint8_t m_unknown0C[0x20 - 0x0C];

        // 0x0054FFD0: the constructor (the name's). Returns this.
        Leader* Construct();
        // 0x0054FFE0 is its destructor: a jump to Str::String's (string.cpp).
    };

    Script::CStruct* m_params; // +0x00: StartCompetition's parameters
    int m_runs;                // +0x04: StartCompetitionRun counts them
    Leader m_leaders[8];       // +0x08
    uint8_t m_unknown108[0x124 - 0x108];
    uint32_t m_unknown124;     // +0x124 (cleared when a run starts)
    uint8_t m_unknown128[0x150 - 0x128];
    uint32_t m_unknown150;     // +0x150 (the same)

    // 0x0054FFF0: the destructor body - the leaders' names, last first.
    void DestroyBody();
    // 0x00550010: copies of `params`' components added to the competition's parameters (if any).
    void EditParams(Script::CStruct* params);
    // 0x00550020: a run starts.
    void StartRun();
};
static_assert(sizeof(CCompetition::Leader) == 0x20 && offsetof(CCompetition, m_leaders) == 0x08 &&
                  offsetof(CCompetition, m_unknown124) == 0x124 && offsetof(CCompetition, m_unknown150) == 0x150,
              "layout must match the original");

} // namespace Mdl
