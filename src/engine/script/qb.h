#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>
#include "engine/core/slab_bank.h"
#include "engine/script/cell.h"
#include "engine/script/tokens.h"
#include "engine/core/memory_sizes.h"

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

// The token bytes of compiled scripts. Names for the plain operators and brackets are the symbol our NeverScript
// decompiler prints for the byte (0x07 prints `=`, 0x42 `:` ...); the others are named for what the interpreter does with
// them. "Meaning unknown": stepped over as one byte, nothing in the game's code acts on them.
constexpr uint8_t kTokenEndOfFile = 0x00;
constexpr uint8_t kTokenEndOfLine = 0x01;
constexpr uint8_t kTokenEndOfLineNumbered = 0x02; // + a 4-byte line number
constexpr uint8_t kTokenStructStart = 0x03;       // prints `{`
constexpr uint8_t kTokenStructEnd = 0x04;         // `}`
constexpr uint8_t kTokenArrayStart = 0x05;        // `[`
constexpr uint8_t kTokenArrayEnd = 0x06;          // `]`
constexpr uint8_t kTokenEquals = 0x07;            // `=`
constexpr uint8_t kTokenDot = 0x08;               // `.`
constexpr uint8_t kTokenComma = 0x09;             // `,`
constexpr uint8_t kTokenMinus = 0x0A;             // `-`
constexpr uint8_t kTokenPlus = 0x0B;              // `+`
constexpr uint8_t kTokenDivide = 0x0C;            // `/`
constexpr uint8_t kTokenMultiply = 0x0D;          // `*`
constexpr uint8_t kTokenOpenParenthesis = 0x0E;   // `(`
constexpr uint8_t kTokenCloseParenthesis = 0x0F;  // `)`
constexpr uint8_t kTokenUnknown10 = 0x10;         // meaning unknown
constexpr uint8_t kTokenEqualTo = 0x11;           // prints `=` too (a comparison)
constexpr uint8_t kTokenLessThan = 0x12;          // `<`
constexpr uint8_t kTokenLessThanOrEqual = 0x13;   // `<=`
constexpr uint8_t kTokenGreaterThan = 0x14;       // `>`
constexpr uint8_t kTokenGreaterThanOrEqual = 0x15; // `>=`
constexpr uint8_t kTokenName = 0x16;              // + a 4-byte checksum
constexpr uint8_t kTokenInteger = 0x17;           // + 4 bytes
constexpr uint8_t kTokenHexInteger = 0x18;        // + 4 bytes
constexpr uint8_t kTokenFloat = 0x1A;             // + 4 bytes
constexpr uint8_t kTokenString = 0x1B;            // + a 4-byte length, the text
constexpr uint8_t kTokenLocalString = 0x1C;       // the same, localised text
constexpr uint8_t kTokenVector = 0x1E;            // + 3 floats
constexpr uint8_t kTokenPair = 0x1F;              // + 2 floats
constexpr uint8_t kTokenLoopStart = 0x20;         // Begin (a loop's start)
constexpr uint8_t kTokenLoopRepeat = 0x21;        // Repeat (a loop's end)
constexpr uint8_t kTokenLoopBreak = 0x22;         // Break
constexpr uint8_t kTokenScriptStart = 0x23;       // a script: + name checksum (after one byte), the code
constexpr uint8_t kTokenScriptEnd = 0x24;         // the end of a script
constexpr uint8_t kTokenIf = 0x25;                // an if with a long jump
constexpr uint8_t kTokenElse = 0x26;
constexpr uint8_t kTokenUnknown27 = 0x27; // meaning unknown
constexpr uint8_t kTokenEndIf = 0x28;
constexpr uint8_t kTokenReturn = 0x29;
constexpr uint8_t kTokenChecksumName = 0x2B;  // + a checksum and its text, in the file's name table
constexpr uint8_t kTokenAllArguments = 0x2C;  // the caller's whole parameter list
constexpr uint8_t kTokenLocalArgument = 0x2D; // one of the caller's parameters, by name
constexpr uint8_t kTokenLongJump = 0x2E;      // + a 4-byte offset
constexpr uint8_t kTokenRandom = 0x2F;        // weighted choice among branches
constexpr uint8_t kTokenRandomRange = 0x30;   // a random number between two floats
constexpr uint8_t kTokenOr = 0x32;
constexpr uint8_t kTokenAnd = 0x33;
constexpr uint8_t kTokenXor = 0x34; // prints `^`
constexpr uint8_t kTokenUnknown35 = 0x35; // meaning unknown
constexpr uint8_t kTokenUnknown36 = 0x36; // meaning unknown
constexpr uint8_t kTokenRandomSideStream = 0x37; // Random, from the second random stream
constexpr uint8_t kTokenRandomRangeSideStream = 0x38; // RandomRange, from the second random stream
constexpr uint8_t kTokenNot = 0x39;                   // prints `!`
constexpr uint8_t kTokenUnknown3A = 0x3A;             // meaning unknown
constexpr uint8_t kTokenUnknown3B = 0x3B;             // meaning unknown
constexpr uint8_t kTokenSwitch = 0x3C;
constexpr uint8_t kTokenEndSwitch = 0x3D;
constexpr uint8_t kTokenCase = 0x3E;
constexpr uint8_t kTokenDefault = 0x3F;
constexpr uint8_t kTokenRandomNoRepeat = 0x40; // a choice that is never the last one's
constexpr uint8_t kTokenRandomPermute = 0x41;  // choices in a shuffled order
constexpr uint8_t kTokenColon = 0x42;          // prints `:`
constexpr uint8_t kTokenNativeFunction = 0x43; // a name looked up: a C function (+ 4 bytes)
constexpr uint8_t kTokenMemberFunctionName = 0x44; // a member function without a table entry (+ its name)
constexpr uint8_t kTokenSectionMarker = 0x45;      // + one byte, a 4-byte section name
constexpr uint8_t kTokenMemberFunction = 0x46;     // a name looked up: a member function's table entry (+ 4 bytes)
constexpr uint8_t kTokenShortIf = 0x47;            // an if with a 2-byte jump
constexpr uint8_t kTokenShortElse = 0x48;
constexpr uint8_t kTokenShortJump = 0x49;

// Token sizes: the byte and what follows it.
constexpr int kTokenWithWordBytes = 5;      // name, integer, float, long jump, tables' header, resolved names
constexpr int kTokenWithPairBytes = 9;      // the byte and two floats
constexpr int kTokenWithVectorBytes = 13;   // the byte and three floats
constexpr int kTokenWithNamedWordBytes = 6; // the byte, one more byte and a 4-byte name (scripts, <name>, markers)
constexpr int kJumpTableEntryBytes = 6;     // a random table's entry: its weight, then its jump
constexpr int kShuffleSwapsPerChoice = 10; // a shuffle (RandomPermute, the shuffle commands) swaps this many times per choice

// The token after `token`.
Tokens step_over_token(Tokens token);
// Past any end-of-line tokens.
Tokens skip_newline_tokens(Tokens token);
// Past the end of the statement starting at `token` (its end of line, outside any brackets).
Tokens skip_to_end_of_statement(Tokens token);
// Past the script's '$' end token (and a line number after it).
Tokens skip_to_end_of_script(Tokens token);

// A script in the symbol table (a script body symbol's value): the record {u32 checksum, u32 size, u32 stored size,
// the code - LZSS-compressed when stored < size}, as define_script_symbol makes it and pre-compiled images hold it.
struct StoredScript {
    uint32_t checksum_ = 0;
    uint32_t size_ = 0;
    uint32_t stored_size_ = 0;
    std::span<const uint8_t> code_; // `stored_size_` bytes
};
constexpr size_t kStoredScriptHeader = 12;
// The record at `record`, read from its bytes.
StoredScript read_stored_script(const uint8_t* record);
// A checksum of the script's tokens (line ends left out), up to its '$'.
uint32_t calculate_script_checksum(Tokens token);
// Replace the names at the starts of statements that name C functions / member functions with direct
// references (tokens 0x43 / 0x46, or 0x44 for a member function without a table entry) - in the script's `code`.
void prepare_script_tokens(std::span<uint8_t> code);

// Little-endian values in the token stream; the writers return the position after the value.
uint32_t fetch_word32(Tokens p);
// What precompiled bytecode holds in place of a name once fixed up (tokens 0x46 / 0x43: the word after the token):
// a member function's table entry, a C function (token_reader.cpp).
const MemberFunctionEntry* member_entry_at(Tokens p);
NativeFunction native_function_at(Tokens p);
uint16_t fetch_word16(Tokens p);
MutableTokens store_word32(MutableTokens p, uint32_t value);
MutableTokens store_word16(MutableTokens p, uint16_t value);

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
void allocate_permanent_string_heap(const memory::sizes::Pools& sizes);

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

constexpr size_t kStoredRandomNumOffset = 8;
constexpr size_t kStoredRandomOrderOffset = 0x12;
constexpr size_t kStoredRandomOrderSize = 0x42;
constexpr size_t kStoredRandomLastOffset = 0x54;
constexpr size_t kStoredRandomSuccOffset = 0x58;
constexpr size_t kStoredRandomBytes = 0x60;
// A stored random sequence (the scripts' RandomNoRepeat / RandomPermute): a shuffled order of choices.
struct StoredRandom {

    const Script* owner_;         // the script using it (null: none)
    const uint8_t* lookup_table_; // the random table's offsets in the script
    uint16_t num_;
    uint16_t unknowna_;
    uint32_t kind_tag_;      // the token: 0x40 no-repeat, 0x41 permute
    uint16_t ordinal_;       // permute: the position in order_
    std::array<uint8_t, kStoredRandomOrderSize> order_;    // permute: the shuffled order (from +0x12)
    uint32_t last_;          // no-repeat: the last choice
    StoredRandom* succ_;     // older
    StoredRandom* previous_; // newer
};

// Shuffle the order; with `avoid_repeat`, the new first choice isn't the old last one.
void shuffle(StoredRandom& random, bool avoid_repeat);

} // namespace script
