// The process's flags: see process_flags.h.

#include "platform/process_flags.h"

#include <string>

namespace sys {

namespace {

bool& stored_internet_game_host()
{
    static bool internet_game_host{};
    return internet_game_host;
}

bool& stored_joining_internet_game()
{
    static bool joining_internet_game{};
    return joining_internet_game;
}

bool& stored_app_active()
{
    static bool app_active{};
    return app_active;
}

std::string& startup_command_line_copy()
{
    static std::string value;
    return value;
}

} // namespace

bool internet_game_host() { return stored_internet_game_host(); }
void set_internet_game_host(bool value) { stored_internet_game_host() = value; }
bool joining_internet_game() { return stored_joining_internet_game(); }
void set_joining_internet_game(bool value) { stored_joining_internet_game() = value; }
bool app_active() { return stored_app_active(); }
void set_app_active(bool value) { stored_app_active() = value; }
const std::string& startup_command_line() { return startup_command_line_copy(); }
void set_startup_command_line(const char* text) { startup_command_line_copy() = text; }

} // namespace sys
