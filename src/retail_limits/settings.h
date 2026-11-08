#pragma once

// NOT ORIGINAL - the retail-limits DLL's settings: thug2-limits.ini next to the game's exe (written with the defaults
// when there is none). docs/retail-limits.md lists the keys.

#include <string>

#include "retail_limits/limits.h"

namespace retail_limits {

struct Settings {
    bool enabled = true; // [limits] enabled=0: nothing is patched
    Raises raises;       // [limits] script_pools= hash_items= script_heap= node_hash= rail_table= shadow_buffers=
    std::string chain;   // [loader] chain=: another DLL of the game folder to load and pass DirectInput to
};

Settings read_settings(const std::string& ini_path);

} // namespace retail_limits
