#pragma once

#include <cstdint>

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
namespace Mem {
struct PoolPair;
}

namespace Script {

// The token after `token`.
const uint8_t* SkipToken(const uint8_t* token);
// Past any end-of-line tokens.
const uint8_t* SkipEndOfLines(const uint8_t* token);
// Past the end of the statement starting at `token` (its end of line, outside any brackets).
const uint8_t* SkipToEndOfStatement(const uint8_t* token);
// Past the script's '$' end token (and a line number after it).
const uint8_t* SkipToEndOfScript(const uint8_t* token);
// A checksum of the script's tokens (line ends left out), up to its '$'.
uint32_t CalculateScriptChecksum(const uint8_t* token);
// Replace the names at the starts of statements that name C functions / member functions with direct
// references (tokens 0x43 / 0x46, or 0x44 for a member function without a table entry).
void PreProcessScript(uint8_t* token);

// Little-endian values in the token stream; the writers return the position after the value.
uint32_t Read4Bytes(const uint8_t* p);
uint16_t Read2Bytes(const uint8_t* p);
uint8_t* Write4Bytes(uint8_t* p, uint32_t value);
uint8_t* Write2Bytes(uint8_t* p, uint16_t value);

// The names of checksums given in the .qb files themselves (tokens 0x2B): a table of 0x1000 buckets.
void AddScriptChecksumName(uint32_t checksum, const char* name);
const char* FindScriptChecksumName(uint32_t checksum);
void FreeScriptChecksumNames();

// Forget which script the stored random states belonging to `owner` (a script) were for.
void ForgetStoredRandomsOf(uint32_t owner);

// Create all the script system's item pools (from the script heap) and the permanent string heap.
void CreateScriptPools();
// Create a pool of `count` items of `item_size` as `pools`' current pool (0x0046F090.. one per class).
void CreatePool(Mem::PoolPair& pools, int item_size, int count, const char* name);
// The permanent string heap: `size` bytes of strings, at most `count` of them.
void AllocatePermanentStringHeap(uint32_t size, uint32_t count);

// The script cache (null when there isn't one), and dropping a script from it (call only with a cache).
void* ScriptCache();
void RemoveFromScriptCache(uint32_t name);

// Symbol bookkeeping while loading .qb files.
struct CSymbolTableEntry;
void AllocateScriptChecksumNames(bool debugHeap);
void FreePair(void* pair);
void FreeVector(void* vector);
void FreeStoredRandom(void* random);
void FreeSymbol(CSymbolTableEntry* symbol);
// Take `symbol` out of the table and free it and its value.
void CleanUpAndRemoveSymbol(CSymbolTableEntry* symbol);
// Pending changes, done by ProcessPendingSymbols once a file is loaded: remove `symbol`, or replace the
// table's symbol of the same name with `symbol`.
void RemoveSymbolLater(CSymbolTableEntry* symbol);
void ReplaceSymbolLater(CSymbolTableEntry* symbol);
void ProcessPendingSymbols();
void ClearScriptFlag6F2564();
void SetScriptFlag6F2564();

// A stored random sequence (the scripts' RandomNoRepeat / RandomPermute): a shuffled order of choices.
struct CStoredRandom {
    // Shuffle the order; with `avoidRepeat`, the new first choice isn't the old last one.
    void Shuffle(bool avoidRepeat);

    uint32_t m_owner;         // the script using it
    const uint8_t* m_table;   // the random table's offsets in the script
    uint16_t m_count;
    uint16_t unknownA;
    uint32_t m_type;          // the token: 0x40 no-repeat, 0x41 permute
    uint16_t m_index;         // permute: the position in m_order
    uint8_t m_order[0x42];    // permute: the shuffled order (from +0x12)
    uint32_t m_last;          // no-repeat: the last choice
    CStoredRandom* m_next;    // older
    CStoredRandom* m_previous; // newer
};

} // namespace Script
