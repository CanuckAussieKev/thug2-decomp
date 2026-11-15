#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// NOT ORIGINAL - a texture set's record (engine/gfx/texture_sets.h): thug2-decomp-textures\<set>\set.json, written by
// tools/pipeline/hd_textures/base.ts as JSON:
//   { "model": "seedvr2", "match": true, "delight": 2,
//     "files": [ { "name": "levels\\bo\\bo.tex.xbx", "original": "<sha-256>", "installed": "<sha-256>",
//                  "delit": ["0a1b2c3d", ...] }, ... ] }
// "delight" (base.ts --delight): the de-lighting version the set's textures were made with (absent: none); a file's
// "delit": the checksums (hex) of its textures whose painted light was taken out before upscaling - the others in it
// keep the original's colour (too small, tiled both ways, cut-outs, skies ... - hd_textures/delight.ts). Only what the
// game reads is kept; other members are skipped.
namespace texture_set_record {

struct File {
    std::string name;            // the game's name for the dictionary (lower case, '\' - as the data overlay keeps it)
    std::vector<uint32_t> delit; // its de-lit textures' checksums, sorted
};

struct Record {
    int delight = 0; // the de-lighting version (0: not de-lit)
    std::vector<File> files;
};

// The record from set.json's text. False (and `record` left empty) if it isn't a JSON object, or is damaged.
[[nodiscard]] bool read(std::string_view json, Record* record);

// The textures de-lit in the file `name` (compared without regard to case, '/' as '\'); empty for a file the record
// doesn't list or one with none.
[[nodiscard]] std::vector<uint32_t> delit_of(const Record& record, std::string_view name);

} // namespace texture_set_record
