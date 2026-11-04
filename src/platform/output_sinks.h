#pragma once

// NOT ORIGINAL - the OS behind the base layer's output (engine/core/dbg.h's debugger messages, engine/core/log_sink.h's
// error lines), installed first thing in WinMain.
namespace platform {

void install_output_sinks();

} // namespace platform
