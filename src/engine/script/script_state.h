#pragma once

#include <array>
#include <cstdint>
#include "engine/script/cache.h"
#include "engine/script/debug_cfuncs.h"
#include "engine/script/game_cfuncs.h"
#include "engine/script/skate_cfuncs.h"
#include "engine/script/param_struct.h"
#include "engine/script/qb.h"
#include "engine/script/token_reader.h"

// NOT ORIGINAL - the script runtime's state its files share: the script cache's pools and store, the permanent strings,
// the stored randoms and the expression evaluator, and the script commands' level files, node list name, join flag and
// last omnigon broadcast (the original's globals).
namespace script {

// The script runtime's state its files share: the script cache's pools and store, the permanent strings, the stored
// randoms and the expression evaluator, and the script commands' level files, node list name, join flag and last
// omnigon broadcast. Made on first use, never destroyed.
class ScriptState {
public:
    memory::PoolPair& cache_entry_pools() { return cache_entry_pools_; }
    std::unique_ptr<script::ScriptStore>& script_store() { return script_store_; }
    std::array<uint32_t, 15>& level_qb_files() { return level_qb_files_; }
    bool& trying_to_join() { return trying_to_join_; }
    std::array<char, 128>& node_list_name() { return node_list_name_; }
    uint32_t& last_broadcasted_omnigon() { return last_broadcasted_omnigon_; }
    char*& permanent_strings_start() { return permanent_strings_start_; }
    char*& permanent_strings_next() { return permanent_strings_next_; }
    uint32_t& permanent_strings_size() { return permanent_strings_size_; }
    uint32_t& permanent_string_count() { return permanent_string_count_; }
    memory::PoolPair& pair_pools() { return pair_pools_; }
    PermanentString*& permanent_string_table() { return permanent_string_table_; }
    [[nodiscard]] uint32_t permanent_string_capacity() const { return permanent_string_capacity_; }
    void set_permanent_string_capacity(uint32_t value) { permanent_string_capacity_ = value; }
    memory::PoolPair& stored_random_pools() { return stored_random_pools_; }
    StoredRandom*& stored_randoms_newest() { return stored_randoms_newest_; }
    ValueResolver& evaluator() { return evaluator_; }

private:
    // (cache.cpp)
    memory::PoolPair cache_entry_pools_{};
    std::unique_ptr<script::ScriptStore> script_store_{};

    // (debug_cfuncs.cpp)
    std::array<uint32_t, 15> level_qb_files_{};

    // (game_cfuncs.cpp)
    bool trying_to_join_{};
    std::array<char, 128> node_list_name_{};

    // (game_flow_cfuncs.cpp)
    uint32_t last_broadcasted_omnigon_{};

    // (param_struct.cpp)
    char* permanent_strings_start_{};
    char* permanent_strings_next_{};
    uint32_t permanent_strings_size_{};
    uint32_t permanent_string_count_{};
    memory::PoolPair pair_pools_{};
    PermanentString* permanent_string_table_{};
    uint32_t permanent_string_capacity_{};

    // (qb.cpp)
    memory::PoolPair stored_random_pools_{};

    // (token_reader.cpp)
    StoredRandom* stored_randoms_newest_{};
    ValueResolver evaluator_{};
};

// The state (made on first use, never destroyed).
ScriptState& script_state();

} // namespace script
