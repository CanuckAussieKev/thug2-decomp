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
constexpr size_t kLevelQbFileCount = 15;
constexpr size_t kNodeListNameBytes = 128;
class ScriptState {
public:
    memory::PoolPair& cache_entry_pools() { return cache_entry_pools_; }
    std::unique_ptr<script::ScriptStore>& script_store() { return script_store_; }
    std::array<uint32_t, kLevelQbFileCount>& level_qb_files() { return level_qb_files_; }
    [[nodiscard]] bool trying_to_join() const { return trying_to_join_; }
    void set_trying_to_join(bool value) { trying_to_join_ = value; }
    std::array<char, kNodeListNameBytes>& node_list_name() { return node_list_name_; }
    [[nodiscard]] uint32_t last_broadcasted_omnigon() const { return last_broadcasted_omnigon_; }
    void set_last_broadcasted_omnigon(uint32_t value) { last_broadcasted_omnigon_ = value; }
    [[nodiscard]] char* permanent_strings_start() const { return permanent_strings_start_; }
    void set_permanent_strings_start(char* value) { permanent_strings_start_ = value; }
    [[nodiscard]] char* permanent_strings_next() const { return permanent_strings_next_; }
    void set_permanent_strings_next(char* value) { permanent_strings_next_ = value; }
    [[nodiscard]] uint32_t permanent_strings_size() const { return permanent_strings_size_; }
    void set_permanent_strings_size(uint32_t value) { permanent_strings_size_ = value; }
    uint32_t& permanent_string_count() { return permanent_string_count_; }
    memory::PoolPair& pair_pools() { return pair_pools_; }
    PermanentString*& permanent_string_table() { return permanent_string_table_; }
    [[nodiscard]] uint32_t permanent_string_capacity() const { return permanent_string_capacity_; }
    void set_permanent_string_capacity(uint32_t value) { permanent_string_capacity_ = value; }
    memory::PoolPair& stored_random_pools() { return stored_random_pools_; }
    [[nodiscard]] StoredRandom* stored_randoms_newest() const { return stored_randoms_newest_; }
    void set_stored_randoms_newest(StoredRandom* value) { stored_randoms_newest_ = value; }
    ValueResolver& evaluator() { return evaluator_; }

private:
    // (cache.cpp)
    memory::PoolPair cache_entry_pools_{};
    std::unique_ptr<script::ScriptStore> script_store_;

    // (debug_cfuncs.cpp)
    std::array<uint32_t, kLevelQbFileCount> level_qb_files_{};

    // (game_cfuncs.cpp)
    bool trying_to_join_{};
    std::array<char, kNodeListNameBytes> node_list_name_{};

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
