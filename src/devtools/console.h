#pragma once

#include <string>

namespace ext {
class Extension;
}

// NOT ORIGINAL - the developer console: ` (the key left of 1; [Keybinds] Console= another - an SDL scan code or a key's
// name) opens and closes it over the game, which reads no key or controller while it is open. A line typed and Enter
// runs a command (`help` lists them); Esc closes it. Drawn with Dear ImGui (engine/ui/ui_backend.h), in its own
// context. [features] console=0 turns it off.
//
// Commands:
//   scripts [text]  the scripts running now (those whose names hold `text`), each level of each call stack with the
//                   interpreter running it - QB or TS (a TypeScript module's level: [features] ts_scripts=1)
//   clear           the console's lines cleared
//   help            the commands
namespace devtools::console {

[[nodiscard]] bool is_switched_on();
// A command's output (also what the console prints for it) - for the tests and the automation channel.
[[nodiscard]] std::string run_command(const std::string& line);
// A line as if typed: shown in the console with its output (the automation channel's `console`); the output.
std::string submit(const std::string& line);

ext::Extension& extension();

} // namespace devtools::console
