// NOT ORIGINAL - the editor mode (editor_mode.h).

#include "engine/sys/editor_mode.h"

#include <cstddef>
#include <string>

#include "base/core/checked_index.h"
#include "platform/process_flags.h"
#include "platform/run_log.h"

namespace sys::editor_mode {

namespace {

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
    static const bool kAsked =
        has_flag(sys::startup_command_line(), "editor") || config_int("features", "editor_mode", 0) != 0;
    return kAsked;
}

bool on() { return mode_on(); }

void set_on(bool on) { mode_on() = on; }

} // namespace sys::editor_mode
