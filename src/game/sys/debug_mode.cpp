// Debug mode (game/sys/debug_mode.h).

#include "game/sys/debug_mode.h"

#include <cstdint>
#include <cstring>

#include "game/core/crc.h"
#include "hook/hook.h"
#include "hook/log.h"

namespace Sys::DebugMode {

namespace {

// The table's CD entry: {name 0x00655C24 "CD", function 0x005B9760}.
constexpr uint32_t kCDEntry = 0x0067FF38;

// The start-up scripts, which keep the disc build's CD: false there makes default_system_startup take the
// developers' path - no UsePreFilesForLevelLoading (their loose files: the retail data's archives aren't loaded, the
// front end's peds lose their animations - a crash at 0x00482BA0), auto_launch_viewer, the script debugger -
// instead of the main menu; autolaunch and startup_loading_screen run with it.
bool IsStartupScript(const Script::CScript* script)
{
    if (!script)
        return false;
    // (the script it's running now: CScript +0xC8)
    uint32_t name = *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(script) + 0xC8);
    static const uint32_t kStartup[] = {Crc::GenerateCRCFromString("default_system_startup"),
                                        Crc::GenerateCRCFromString("autolaunch"),
                                        Crc::GenerateCRCFromString("startup_loading_screen")};
    for (uint32_t startup : kStartup) {
        if (name == startup)
            return true;
    }
    return false;
}

} // namespace

bool& Enabled()
{
    static bool on = ConfigInt("features", "debug_mode", 0) != 0;
    return on;
}

bool __cdecl CFunc_CD(Script::CStruct* /*params*/, Script::CScript* script)
{
    return !Enabled() || IsStartupScript(script); // (the original: true - 0x005B9760)
}

void Install()
{
    if (!Enabled())
        return;
    if (std::strcmp(*reinterpret_cast<const char* const*>(kCDEntry), "CD") != 0)
        return; // (not this exe's table)
    auto function = reinterpret_cast<uint32_t>(&CFunc_CD);
    WriteGameCode(kCDEntry + 4, &function, sizeof(function));
    Log("debug mode: on (CD is false: the developers' menus - DEBUG OPTIONS in the pause menu)");
}

} // namespace Sys::DebugMode
