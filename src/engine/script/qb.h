#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include "engine/core/slab_bank.h"
#include "engine/script/cell.h"

// Compiled scripts (.qb bytecode): reading and skipping tokens, and setting up the script system's pools.
//
// A script is a stream of one-byte tokens, some followed by data (little-endian, unaligned). The ones these
// helpers care about:
//   0x00 end of file?        0x01 end of line          0x02 end of line + 4-byte line number
//   0x03 / 0x04  { }         0x05 / 0x06  [ ]          0x0E / 0x0F  ( )
//   0x16 name (4-byte checksum)   0x17 integer   0x18 hex integer   0x1A float      (+4 bytes)
//   0x1B / 0x1C string / local string (4-byte length, then the text)
//   0x1E vector (3 floats)   0x1F pair (2 floats)   0x24 '$' end of script
//   0x2B a checksum's name (4-byte checksum, then the text)
//   0x2F / 0x37 / 0x40 / 0x41 jump tables (4-byte count, then 6 bytes each)
//   0x43 / 0x44 / 0x46 a name already looked up: C function / member function (4-byte value)
//   0x47 / 0x48 / 0x49 short jumps (2-byte offset)
namespace memory {
struct PoolPair;
}

namespace script {

class Script;

// The token after `token`.
const uint8_t* step_over_token(const uint8_t* token);
// Past any end-of-line tokens.
const uint8_t* skip_newline_tokens(const uint8_t* token);
// Past the end of the statement starting at `token` (its end of line, outside any brackets).
const uint8_t* skip_to_end_of_statement(const uint8_t* token);
// Past the script's '$' end token (and a line number after it).
const uint8_t* skip_to_end_of_script(const uint8_t* token);

// A script in the symbol table (a script body symbol's value): the record {u32 checksum, u32 size, u32 stored size,
// the code - LZSS-compressed when stored < size}, as define_script_symbol makes it and pre-compiled images hold it.
struct StoredScript {
    uint32_t checksum_ = 0;
    uint32_t size_ = 0;
    uint32_t stored_size_ = 0;
    const uint8_t* code_ = nullptr; // `stored_size_` bytes
};
constexpr size_t kStoredScriptHeader = 12;
// The record at `record`, read from its bytes.
StoredScript read_stored_script(const uint8_t* record);
// A checksum of the script's tokens (line ends left out), up to its '$'.
uint32_t calculate_script_checksum(const uint8_t* token);
// Replace the names at the starts of statements that name C functions / member functions with direct
// references (tokens 0x43 / 0x46, or 0x44 for a member function without a table entry).
void prepare_script_tokens(uint8_t* token);

// Little-endian values in the token stream; the writers return the position after the value.
uint32_t fetch_word32(const uint8_t* p);
// What precompiled bytecode holds in place of a name once fixed up (tokens 0x46 / 0x43: the word after the token):
// a member function's table entry, a C function (token_reader.cpp).
const MemberFunctionEntry* member_entry_at(const uint8_t* p);
NativeFunction native_function_at(const uint8_t* p);
uint16_t fetch_word16(const uint8_t* p);
uint8_t* store_word32(uint8_t* p, uint32_t value);
uint8_t* store_word16(uint8_t* p, uint16_t value);

// The names of checksums given in the .qb files themselves (tokens 0x2B): a table of 0x1000 buckets.
void add_script_checksum_name(uint32_t checksum, const char* name);
const char* find_script_checksum_name(uint32_t checksum);
void free_script_checksum_names();
// NOT ORIGINAL - every .qb's checksum names kept for our tools (the level editor's HUD: node names such as
// Fire_Tender), once keep_qb_names(true) - read_qb_file notes each (the game only keeps them with its debug names on).
void keep_qb_names(bool keep);
void note_qb_name(uint32_t checksum, const char* name);
const char* kept_qb_name(uint32_t checksum);

// Forget which script the stored random states belonging to `owner` (a script) were for.
void forget_stored_randoms_of(const Script* owner);

// Create all the script system's item pools (from the script heap) and the permanent string heap.
void create_script_pools();
// Create a pool of `count` items of `item_size` as `pools`' current pool (0x0046F090.. one per class).
void create_pool(memory::PoolPair& pools, int item_size, int count, const char* name);
// NOT ORIGINAL - the script pools' use logged (`script pools in use <when>: CComponent <used> (peak <peak>) of
// <count>, CStruct ...`): what a level costs them (frontend/start_level.cpp logs it once the level is up).
void log_script_pool_use(const char* when);
// The permanent string heap: `size` bytes of strings, at most `count` of them.
void allocate_permanent_string_heap(uint32_t size, uint32_t count);

// The script cache (null when there isn't one), and telling it a script has changed (call only with a cache).
class ScriptStore;
ScriptStore* script_store_instance();
void remove_from_script_cache(uint32_t name);

// Symbol bookkeeping while loading .qb files.
struct SymbolTableEntry;
void allocate_script_checksum_names(bool debug_heap);
void free_pair(void* pair);
void free_vector(void* vector);
void free_stored_random(void* random);
void free_symbol(SymbolTableEntry* symbol);
// Take `symbol` out of the table and free it and its value.
void clean_up_and_remove_symbol(SymbolTableEntry* symbol);
// Pending changes, done by process_pending_symbols once a file is loaded: remove `symbol`, or replace the
// table's symbol of the same name with `symbol`.
void remove_symbol_later(SymbolTableEntry* symbol);
void replace_symbol_later(SymbolTableEntry* symbol);
void process_pending_symbols();
void clear_script_flag_6f_2564();
void set_script_flag_6f_2564();

// A stored random sequence (the scripts' RandomNoRepeat / RandomPermute): a shuffled order of choices.
struct StoredRandom {
    // Shuffle the order; with `avoid_repeat`, the new first choice isn't the old last one.
    void shuffle(bool avoid_repeat);

    const Script* owner_;         // the script using it (null: none)
    const uint8_t* lookup_table_; // the random table's offsets in the script
    uint16_t num_;
    uint16_t unknowna_;
    uint32_t kind_tag_;      // the token: 0x40 no-repeat, 0x41 permute
    uint16_t ordinal_;       // permute: the position in order_
    std::array<uint8_t, 0x42> order_;    // permute: the shuffled order (from +0x12)
    uint32_t last_;          // no-repeat: the last choice
    StoredRandom* succ_;     // older
    StoredRandom* previous_; // newer
};

extern memory::PoolPair g_stored_random_pools;

} // namespace script
