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
//   rt summary      ray tracing's contract check ([debug] rt_contract_check=1, rt_check.h): its counts, violations
//   rt dump [file]  the last frame's ray-tracing record as JSON in the game folder
//   rt sun <x y z> | off  every effect's sun set by hand (toward it; rt_scene.h set_sun_override); `rt sun` alone: the
//                   scene's sun and the one each effect used last; `rt sun source <game | level | x,y,z | h,e>`:
//                   rt_sun_source for this run
//   rt tod [<tod> | cycle <on | off>]  the time of day for this run (rt_daylight.h; [Graphics] tod's words or HH:MM),
//                   the clock running or not; alone: the clock's state (day_clock.h)
//   help            the commands
namespace devtools::console {

[[nodiscard]] bool is_switched_on();
// A command's output (also what the console prints for it) - for the tests and the automation channel.
[[nodiscard]] std::string run_command(const std::string& line);
// A line as if typed: shown in the console with its output (the automation channel's `console`); the output.
std::string submit(const std::string& line);

ext::Extension& extension();

} // namespace devtools::console
