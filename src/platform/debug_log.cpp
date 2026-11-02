// The PC layer's log.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "platform/debug_log.h"

#include <cstdio>
#include <cstring>
#include <array>

#include "platform/platform_config.h"
#include "platform/run_log.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"

namespace debug_log {

namespace {

// NOT ORIGINAL grouping - debug_log.cpp's state (the original's globals). Made on first use, never destroyed
// (debug_log_state()).
struct DebugLogState {
    bool configured_ = false;
    int level_ = static_cast<int>(StageMap::kInfo);
    bool to_console_ = false;
};

DebugLogState& debug_log_state()
{
    static core::NoDestroy<DebugLogState> state;
    return state.get();
}

} // namespace

void configure(bool to_console)
{
    debug_log_state().to_console_ = to_console;
    debug_log_state().level_ = build_info::get_int(build_info::kLogging, "Level", 2);
    debug_log_state().configured_ = true;
}

bool wants(StageMap level) { return debug_log_state().configured_ && static_cast<int>(level) <= debug_log_state().level_; }

bool console_on() { return debug_log_state().to_console_; }

void write_message(const char* message)
{
    std::array<char, 1024> text{};
    text::format_to(text, "%s", message);
    size_t length = std::strlen(text.data());
    while (length > 0 && (core::at(text, length - 1) == '\n' || core::at(text, length - 1) == '\r'))
        core::at(text, --length) = '\0';
    get_log("pc: %s", text.data());
}

} // namespace debug_log
