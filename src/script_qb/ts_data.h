#pragma once

// NOT ORIGINAL - a script file's data in the game unpacked ([features] ts_scripts=1, docs/qb-ts.md "Data"): its globals
// are <name>.json beside its <name>.ts (tools/qbjs/qb_data.ts - the format; emit_data.ts writes it). The JSON is read
// straight into the script heap's cells - structures, arrays, strings, pairs, vectors - as the TypeScript bridge made
// them from the runtime's value objects (ts_bridge.cpp js_to_cell), with no JavaScript run for them.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "engine/script/cell.h"
#include "engine/script/param_struct.h"

namespace script::qb::ts {

struct DataHooks {
    // A translated text's bytes ({"$tr": token}: the language's, else English's); none - the token itself.
    std::function<std::optional<std::string>(std::string_view token)> text;
    // A structure's script value ({"$script": key}): `component` made the script the module's .ts gave that key
    // (qb_ts.js scriptValue). False: `error` says why.
    std::function<bool(std::string_view key, Component& component, std::string& error)> script_value;
    // A global read: its symbol defined (the cell taken over).
    std::function<void(uint32_t name, QbValueKind kind, Cell& cell)> define;
};

// Every global of data file `json` defined, in the file's order. `from`: the hash of the .qb it was made from (its
// "from"). False when the file isn't one - `error` says where; the globals before that point are defined.
bool define_data(std::string_view json, const DataHooks& hooks, std::string* from, std::string& error);

// UTF-8 text (a JavaScript string's, QuickJS's JS_ToCString) as bytes: each character one byte - the scripts' strings
// (qb_ts.js bytes). A character past 0xFF can't be one: none (as a bad sequence).
std::optional<std::string> bytes_from_utf8(std::string_view text);

} // namespace script::qb::ts
