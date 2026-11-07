// NOT ORIGINAL - the editor mode (editor_mode.h).

#include "engine/sys/editor_mode.h"

#include <windows.h>

#include <array>
#include <cstddef>
#include <string>

#include "base/core/checked_index.h"
#include "platform/process_flags.h"
#include "platform/run_log.h"

namespace sys::editor_mode {

namespace {

constexpr std::size_t kValueSize = 128;

bool& mode_on()
{
    static bool on = false;
    return on;
}

bool separates(char character) { return character == ' ' || character == '\t' || character == '"'; }

} // namespace

bool has_flag(const std::string& line, const char* flag)
{
    const std::string name = flag;
    for (const std::string& form : {"--" + name, "-" + name}) {
        for (std::size_t at = line.find(form); at != std::string::npos; at = line.find(form, at + 1)) {
            const std::size_t end = at + form.size();
            const bool starts = at == 0 || separates(core::at(line, at - 1));
            const bool ends = end == line.size() || separates(core::at(line, end));
            if (starts && ends) {
                return true;
            }
        }
    }
    return false;
}

bool requested()
{
    static const bool kAsked = [] {
        const std::string& line = sys::startup_command_line();
        if (has_flag(line, "editor") || config_int("features", "editor_mode", 0) != 0) {
            return true;
        }
        // (-edit <level> / --edit=<level> / [features] edit_level=<level>: the level editor is the editor mode's only)
        if (has_flag(line, "edit") || line.find("--edit=") != std::string::npos) {
            return true;
        }
        std::array<char, kValueSize> level{};
        GetPrivateProfileStringA("features", "edit_level", "", level.data(), static_cast<DWORD>(level.size()),
                                 game_folder_path("thug2-decomp.ini").c_str());
        return level.front() != '\0';
    }();
    return kAsked;
}

bool on() { return mode_on(); }

void set_on(bool on) { mode_on() = on; }

} // namespace sys::editor_mode
