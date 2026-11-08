// NOT ORIGINAL - the retail-limits DLL's settings (settings.h).

#include "retail_limits/settings.h"

#include <windows.h>

#include <array>
#include <string>
#include <string_view>

#include "retail_limits/limits.h"

namespace retail_limits {

namespace {

constexpr std::string_view kDefaultIni =
    "; The original game's limits raised for large levels (thug2-limits.log says what was done).\r\n"
    "; 1 = on, 0 = off.\r\n"
    "[limits]\r\n"
    "; 0: patch nothing\r\n"
    "enabled=1\r\n"
    "; the script pools and the string table\r\n"
    "script_pools=1\r\n"
    "; the HashItem pool\r\n"
    "hash_items=1\r\n"
    "; 16 MB more script heap\r\n"
    "script_heap=1\r\n"
    "; the node name hash (levels of more than 16,384 named nodes)\r\n"
    "node_hash=1\r\n"
    "; the rail name table (levels of more than 16,384 rail nodes)\r\n"
    "rail_table=1\r\n"
    "\r\n"
    "[loader]\r\n"
    "; another DLL in the game folder to load as well and pass DirectInput to - e.g. a mod's own dinput8.dll,\r\n"
    "; renamed (chain=dinput8_mod.dll). Empty: DirectInput is Windows' own.\r\n"
    "chain=\r\n";

constexpr DWORD kChainChars = 260;

bool flag(const std::string& ini, const char* key, bool fallback)
{
    return GetPrivateProfileIntA("limits", key, fallback ? 1 : 0, ini.c_str()) != 0;
}

void write_defaults(const std::string& ini)
{
    HANDLE file = CreateFileA(ini.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return; // (there is one already, or the folder can't be written)
    }
    DWORD written = 0;
    WriteFile(file, kDefaultIni.data(), static_cast<DWORD>(kDefaultIni.size()), &written, nullptr);
    CloseHandle(file);
}

} // namespace

Settings read_settings(const std::string& ini_path)
{
    write_defaults(ini_path);
    Settings settings;
    settings.enabled = flag(ini_path, "enabled", true);
    settings.raises.script_pools = flag(ini_path, "script_pools", true);
    settings.raises.hash_items = flag(ini_path, "hash_items", true);
    settings.raises.script_heap = flag(ini_path, "script_heap", true);
    settings.raises.node_hash = flag(ini_path, "node_hash", true);
    settings.raises.rail_table = flag(ini_path, "rail_table", true);
    std::array<char, kChainChars> chain = {};
    GetPrivateProfileStringA("loader", "chain", "", chain.data(), kChainChars, ini_path.c_str());
    settings.chain = chain.data();
    return settings;
}

} // namespace retail_limits
