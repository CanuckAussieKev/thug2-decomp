#pragma once

#include <string>
#include <utility>
#include <vector>

#include "base/core/api.h"

// NOT ORIGINAL - programs the game starts and watches (ours: the level editor's Import Level window runs the level
// converters with the Node runtime beside the exe - editor/level_editor_import.h). A child runs
//   - with no window of its own (no console), its standard input empty, its standard output and error read into one
//     text as it writes them (take_output), in the order they came;
//   - in a job object of its own: stop ends it and every program it started in turn (the whole tree), and so does the
//     game ending (the job is closed with the game);
//   - with the game's environment and `environment`'s variables added (or replaced).
// Paths and arguments are UTF-8. Calls from one thread at a time (the editor's frame); the output is read on a thread
// of its own per child.
namespace sys::child_process {

struct Command {
    std::string program;                                         // the exe, a full path
    std::vector<std::string> arguments;                          // after the program's own name
    std::string folder;                                          // its working folder (empty: the game's)
    std::vector<std::pair<std::string, std::string>> environment; // variables added to the game's
};

// Starts it: its number (above 0), or 0 with `why` saying what Windows said.
THUG2_API int start(const Command& command, std::string* why);
// What the child has written since the last call (both streams, as they came).
THUG2_API std::string take_output(int child);
// Whether it has finished - it has exited and all it wrote has been read - and then its exit code.
THUG2_API bool finished(int child, int* exit_code);
// Ends it and everything it started (its exit code then 1).
THUG2_API void stop(int child);
// Forgets it (ended first while it runs).
THUG2_API void release(int child);

// The command line Windows is given: the program and each argument quoted as the C runtime reads them back (exposed
// for the checks).
std::wstring command_line(const Command& command);
std::wstring quoted(const std::wstring& argument);

} // namespace sys::child_process
