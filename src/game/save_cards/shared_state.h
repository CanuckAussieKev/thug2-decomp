#pragma once

// NOT ORIGINAL - the state the files of this folder share (the original's globals), one record per file that
// defines it, each reached through its accessor (made on first use, never destroyed). The header is the folder's
// own: other subsystems use the folder's functions.

#include <array>
#include <cstdint>
#include "game/save_cards/memcard.h"
#include "game/save_cards/memcard_cfuncs.h"

namespace save_cards {

// NOT ORIGINAL grouping - memcard.cpp's state shared with the folder's other files (the original's globals). Made on
// first use, never destroyed (memcard_shared()).
struct MemcardShared {
    Hub* the_card_{};
};
MemcardShared& memcard_shared();

} // namespace save_cards

namespace save_cards {

// NOT ORIGINAL grouping - memcard_cfuncs.cpp's state shared with the folder's other files (the original's globals).
// Made on first use, never destroyed (memcard_cfuncs_shared()).
struct MemcardCfuncsShared {
    bool out_of_space_{};
    script::Struct* kept_params_{};
    uint32_t kept_type_{};
    bool loaded_custom_skater_{};
};
MemcardCfuncsShared& memcard_cfuncs_shared();

} // namespace save_cards
