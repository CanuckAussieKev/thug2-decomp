#pragma once

#include <cstdint>

#include "engine/script/member_code.h"

// The names behind checksums (a debug aid, kept only while the lookup is switched on) and the registration
// of the game's script functions as global symbols.
namespace script {

// Whether checksum names are kept (0x007CEFAC).
bool checksum_names_enabled();
// Make the (empty) name table: 0x10000 buckets of 10 entries, and a 5,000,000-byte block for the names.
void allocate_checksum_name_lookup_tables();
// Remember that `checksum` is the checksum of `name` (a checksum already known is left alone).
void register_name_hash(uint32_t checksum, const char* name);
// The name `checksum` was made from, or null.
const char* look_up_hash_name(uint32_t checksum);
// The name `checksum` was made from, or "Unknown(0x<checksum>)" (in a shared buffer).
const char* find_checksum_name_or_unknown(uint32_t checksum);
// Read a .chk file's lines ("0x<8 hex digits> <name>\r\n") and pass each to `add`.
void parse_checksum_names(const char* text, int size, void (*add)(uint32_t checksum, const char* name));
// Load the names listed in the .chk file next to `file` (a .qb file).
void load_checksum_names(const char* file);

// A C++ function scripts can call by name.
struct NativeCommand {
    const char* name;
    void* function;
};
// Make each function a global symbol (type 8) under its name's checksum.
void install_script_functions(const NativeCommand* functions, uint32_t count);
// Make each name a member-function symbol (type 9).
void publish_method_table(const char* const* names, uint32_t count);
// Make each record of a table (starting with its name's checksum, `stride` bytes each, ended by a 0
// checksum) a member-function symbol whose value is the record.
void register_member_function_table(void* unused, const uint32_t* records, int stride);

// The code half of a member-function table (0x18-byte records: name, component type, then a 16-byte member function
// pointer), as the original's static initialisers write it: record i's function `code[i]` followed by three zero words,
// and after the `count` records the terminator's {0, 0, 0, -1}. (NOT ORIGINAL as a function: each initialiser has
// the stores written out.)
void fill_member_function_code(uint32_t* records, const MemberCode* code, int count);
// Every table's code, in the original's initialiser order (0x00640E30 - 0x006448C0; generated:
// member_function_code.cpp). Run at start-up.
void fill_member_function_tables();

} // namespace script
