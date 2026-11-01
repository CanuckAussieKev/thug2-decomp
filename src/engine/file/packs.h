#pragma once

#include <cstdint>
#include <string>

namespace script {
class Struct;
class Script;
} // namespace script

// "Pip" file loading: data files are loaded whole into memory, either one at a time (up to 200, reference
// counted) or as .pre archives - packs of files loaded in one go (up to 100 at once). Files are found by
// the checksum of their name.
namespace packs {

// A file of a loaded .pre archive: unpacked into the archive's block (16-byte aligned) and used in place. (The
// original kept these records in the block, in the archive's own file-header layout, before each file's name and
// data; the archive as stored is read by formats/pre_format.h.)
struct PackedFile {
    uint32_t checksum{};
    int size{};           // unpacked
    std::string name;     // as the archive names it
    uint8_t* data{};      // in the archive's block
    uint16_t use_count_{}; // how many packs::read_in calls are using it
};

// A file loaded on its own.
struct LoadedFile {
    void* data;
    uint32_t checksum;
    int size;
    int ref_count_;
};

// The file tables' static object (0x007CFFA8, no fields of its own): its constructor, a static initialiser's
// (0x005B70F0), empties the .pre archive and loaded file tables. Returns this.
struct FileTables {
    FileTables();
};

// Load a file into memory, or add a use to it if it's already loaded (on its own or inside a .pre
// archive). Returns its data, or null. `async` loads through the asynchronous file loader, with an
// optional completion callback and two arguments for it.
void* read_in(const char* name, bool async, void* callback, void* hook_user_a, void* hook_user_b);

// Release one use of a file; a file loaded on its own is freed when its last use goes.
void offload(uint32_t checksum);
void offload(const char* name);
// Size of a loaded file (0 if it isn't loaded).
int get_file_size(uint32_t checksum);
int get_file_size(const char* name);

// Load the .pre archive pre\<name> (unless it's loaded already) into the next free slot, repacked so that
// every file in it is stored uncompressed and 16-byte aligned - files are then used in place. French and
// German use a localised archive (.prf/.prd) if there is one.
void load_pre(const char* name);
// Load `name` into `buffer` (or a new block if null) from a .pre archive or disk; `*size` (if given) gets its
// size. Returns the data, or null.
void* load_file_into(const char* name, int* size, void* buffer);

// Free every loaded .pre archive with this name. True if there was one.
bool unload_pre(const char* name);
// The loaded .pre archive after the one named `previous` (null for the first), or null.
const char* take_next_finished_archive(const char* previous);
// True if any file in the named .pre archive is in use.
bool archive_is_busy(const char* name);

// Look a file up in the loaded .pre archives.
PackedFile* find_pre_entry(uint32_t checksum);

// Script functions (in the Function table)
bool cfunc_load_pip_pre(script::Struct* params, script::Script* script);
bool cfunc_un_load_pip_pre(script::Struct* params, script::Script* script);
bool cfunc_dump_pip_pre_status(script::Struct* params, script::Script* script);

} // namespace packs
