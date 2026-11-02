#pragma once

// NOT ORIGINAL - the state the files of this folder share (the original's globals), one record per file that
// defines it, each reached through its accessor (made on first use, never destroyed). The header is the folder's
// own: other subsystems use the folder's functions.

#include <array>
#include <cstdint>
#include "engine/script/cache.h"
#include "engine/script/debug_cfuncs.h"
#include "engine/script/game_cfuncs.h"
#include "engine/script/skate_cfuncs.h"
#include "engine/script/param_struct.h"
#include "engine/script/qb.h"
#include "engine/script/token_reader.h"

namespace script {

// NOT ORIGINAL grouping - cache.cpp's state shared with the folder's other files (the original's globals). Made on
// first use, never destroyed (cache_shared()).
struct CacheShared {
    memory::PoolPair cache_entry_pools_{};
};
CacheShared& cache_shared();

} // namespace script

namespace commands {

// NOT ORIGINAL grouping - debug_cfuncs.cpp's state shared with the folder's other files (the original's globals). Made
// on first use, never destroyed (debug_cfuncs_shared()).
struct DebugCfuncsShared {
    std::array<uint32_t, 15> level_qb_files_{};
};
DebugCfuncsShared& debug_cfuncs_shared();

} // namespace commands

namespace commands {

// NOT ORIGINAL grouping - game_cfuncs.cpp's state shared with the folder's other files (the original's globals). Made
// on first use, never destroyed (game_cfuncs_shared()).
struct GameCfuncsShared {
    bool trying_to_join_{};
    std::array<char, 128> node_list_name_{};
};
GameCfuncsShared& game_cfuncs_shared();

} // namespace commands

namespace commands {

// NOT ORIGINAL grouping - game_flow_cfuncs.cpp's state shared with the folder's other files (the original's globals).
// Made on first use, never destroyed (game_flow_cfuncs_shared()).
struct GameFlowCfuncsShared {
    uint32_t last_broadcasted_omnigon_{};
};
GameFlowCfuncsShared& game_flow_cfuncs_shared();

} // namespace commands

namespace script {

// NOT ORIGINAL grouping - param_struct.cpp's state shared with the folder's other files (the original's globals). Made
// on first use, never destroyed (param_struct_shared()).
struct ParamStructShared {
    char* permanent_strings_start_{};
    char* permanent_strings_next_{};
    uint32_t permanent_strings_size_{};
    uint32_t permanent_string_count_{};
    memory::PoolPair pair_pools_{};
    PermanentString* permanent_string_table_{};
    uint32_t permanent_string_capacity_{};
};
ParamStructShared& param_struct_shared();

} // namespace script

namespace script {

// NOT ORIGINAL grouping - qb.cpp's state shared with the folder's other files (the original's globals). Made on first
// use, never destroyed (qb_shared()).
struct QbShared {
    memory::PoolPair stored_random_pools_{};
};
QbShared& qb_shared();

} // namespace script

namespace script {

// NOT ORIGINAL grouping - token_reader.cpp's state shared with the folder's other files (the original's globals). Made
// on first use, never destroyed (token_reader_shared()).
struct TokenReaderShared {
    StoredRandom* stored_randoms_newest_{};
    ValueResolver evaluator_{};
};
TokenReaderShared& token_reader_shared();

} // namespace script
