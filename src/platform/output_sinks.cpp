#include "platform/output_sinks.h"

#include <cstdio>

#include <windows.h>

#include "engine/core/dbg.h"
#include "engine/core/log_sink.h"
#include "platform/debug_log.h"

namespace platform {

namespace {

void to_debugger(const char* text) { OutputDebugStringA(text); }

// As debug_log::printf(StageMap::kError, ...): when errors are logged, to stdout too if the console is on, and into
// thug2-decomp.log.
void to_run_log(const char* line)
{
    if (!debug_log::wants(debug_log::StageMap::kError)) {
        return;
    }
    if (debug_log::console_on()) {
        std::fputs(line, stdout);
    }
    debug_log::write_message(line);
}

} // namespace

void install_output_sinks()
{
    debug::set_output_sink(&to_debugger);
    core::set_error_line_sink(&to_run_log);
}

} // namespace platform
