#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace frontend {
class UiWidget;
}
namespace modules {
struct FrontEnd;
}
namespace script {
class Struct;
}

// NOT ORIGINAL - the test-automation channel (ours, added at the user's request; not the game's and not PartyMod's):
// tests drive the menus by commands instead of posted key presses, and read where the game is from a status file.
//
// thug2-decomp.ini [features] automation=1 turns it on (default 0: off - the main install never has it on;
// tools/game/make_workspace.ps1 turns it on in the workspaces). While it's on, once a frame (the front end's input
// logic, also paused and in a level) the game looks at <game folder>\thug2-decomp-commands.txt (its size and time
// first) and runs the lines added since, in order; each command and its result go to the log and to
// <game folder>\thug2-decomp-status.json (rewritten when something in it changes, readable while the game runs).
// A command file written more than 10 s before the game started (a last run's) is ignored; tools delete it before
// and after a run and may write commands just before starting the game. If the file gets shorter (rewritten), it's read from the
// start again.
//
// Commands, one a line (keywords in any case; `#` starts a comment; an <id> is a name - its checksum - or 0x<hex>):
//   run <script> [name=value | flag]...   spawn a script; values: 12, 1.5, "text", or a name (a checksum)
//   choose <id> [seconds]                 the menu item <id>: its menu's selection moved there by pad_up / pad_down
//                                         (or pad_left / pad_right) as the pad does, then pad_choose - waits (default
//                                         10 s) for the item to exist and its menu to take pad events; an element not
//                                         in a menu gets pad_choose itself
//   focus <id> [seconds]                  the same without pad_choose (an element not in a menu: a focus event)
//   press <button> [frames]               a pad button held through our input layer (Sio, port 0) for `frames`
//                                         frames (default 3), then up for 3: up down left right x (cross, choose,
//                                         enter, a) circle (back, esc, b) square triangle (y) start (pause) select
//                                         l1 r1 l2 r2 l3 r3
//   hold <button> [frames]                the same button held for `frames` (1-3600) without waiting: the next
//                                         commands (capture, trace...) run while it's down
//   wait <frames>
//   waitfor <condition> [seconds]         until the condition holds (default 90 s; then an error):
//                                         ready (the main menu up, taking pad events for a second: chosen away at
//                                         once, the next menu came up under a loading logo that stayed), frontend (the front end's
//                                         level loaded - SKATE NOW skates there too), ingame (a level other than the
//                                         front end, with the skater, not paused), level <name> (the skate module's
//                                         level is Load_<name>, with the skater) - those two for a second in a row,
//                                         past the level's first frames -, menu <id> (the element exists), focus <id>
//                                         (the element is focused, or a focused menu's selected item)
//   level <name>                          free skate in a level, as -level (devtools/start_level.h)
//   teleport <x> <y> <z> [heading]        the local skater put there STANDING (walking, no momentum), on the ground
//                                         below; heading in degrees (0 = +z, 90 = +x, as the screenshot sidecar);
//                                         none: the way it faced (devtools/test_view.h). In a level only.
//   camera <x> <y> <z> <dx> <dy> <dz> [<vx> <vy> <vz>]
//                                         viewport 0's camera held at that position looking along (dx, dy, dz) every
//                                         frame (comparable frames), moved by (vx, vy, vz) a game frame if given (a
//                                         slow exact move: z-fighting shows); `camera off` lets it go
//   capture <frames> [folder]             the next <frames> rendered frames (1-600), every one, as frame_0001.png ...
//                                         in the screenshot folder's <folder> (default series_<time>), with one
//                                         series.txt (devtools/screenshot.h start_series); done once all are written
//   trace <frames>                        the local skater traced for the next <frames> game frames (1-36000) into
//                                         thug2-decomp-trace.txt (rewritten): a line a frame - frame, position,
//                                         velocity, the up row's y, the SkaterState state (0 ground, 1 air, 2 wall,
//                                         3 lip, 4 rail, 5 wallplant) and physics (0 skating, 1 walking), bailing,
//                                         doing a trick, the score's combo so far, multiplier and +0x20, the last
//                                         ground face's flags and collision object; a `teleport` while it runs
//                                         adds a "# teleport ..." line (tools/game/trace_summary.ts). Done at
//                                         once: the trace runs on under the next commands (`press up 120`...); a
//                                         new trace replaces a running one
//   fe select <screen>/<item> [seconds]   the new front end (frontend/new_frontend.h, [features] new_frontend=1):
//                                         waits (default 10 s) for its screen <screen> to be shown and still, then
//                                         selects and chooses <item> - a list's item id (career, freeskate, story,
//                                         continue, cas, resume ...), a level's load script or name, an option's
//                                         [section.]key or game_options, tab:<game> / group:<group>; screens main,
//                                         career, story, classic, difficulty, confirm, create, online, options, levels,
//                                         pause, pausegoals. fe focus <screen>/<item>: selects only. fe back, fe press
//                                         <up|down|left|right|confirm|back|resume|prevtab|nexttab>. Online's Play
//                                         online is refused. `waitfor fe <screen> [seconds]`: until that screen is
//                                         shown and still.
//   echo <text>                           logged and shown as the result (a marker for tools)
//   quit                                  the game closes (as its window's close button)
//   crash <kind>                          a deliberate fault, to check the crash handling (platform/crash.h): access, rtc,
//                                         crterror, invalidparam, purecall, abort, overrun (the game's /GS box),
//                                         crtbox (the game's CRT message box) - each logged, then the game ends
//                                         without a dialog -, fastfail (ends at once, nothing logged), hang (the
//                                         main thread stops: tools' not-responding check). Only here: the channel
//                                         is on in the workspaces only.
// A failure (a parse error, a refusal, a timeout, an element that never came) ends the batch: the lines after it
// are skipped (counted as done, logged).
//
// Status file (JSON, one object): frames, ready (the main menu has been up), main_menu (it's up now), where
// ("starting", "front_end", "level"), paused, loading (a loading screen is up - choose and press wait for it to go,
// at most 5 s), level (the skate module's level without Load_: Mainmenu, BO...; 0x<hex> if not known), game_mode,
// focus (the focused elements' ids), selected (the focused menus' selected items), lines_read, lines_done, queued,
// current (the command running), last (the last command finished), result ("ok", "error: ...", "refused: ...",
// or the echo's text), errors, refusals. Ids show as names when the name is known (the game's menu ids, game
// modes, levels, names used in commands; tools/game/make_automation_tables.ts), else 0x<hex>.
//
// Command line: -run <script> (or --run <script>, --run=<script>) runs that script once the main menu is up (with
// or without [features] automation; the same safety rules).
//
// Safety (hard rules - tests never go online):
// - nothing online: `run` refuses the scripts of the game's network / online script files and every script, and
//   every parameter value, whose name has an online word between underscores (net, online, internet, lobby,
//   login, logon, buddy, homie, gamespy, network, server, join, host, lan) or internet / lobby / gamespy / network /
//   login anywhere; `choose` / `focus` refuse those ids and any
//   item whose event handlers (or its menus' pad_choose / pad_start handlers) name such a script anywhere in their
//   parameters (MULTIPLAYER, Network / Online Play, the skate shop's net Ready...); `press` of a button that isn't a
//   direction refuses while a focused item is such an item. (Tables: automation_tables.inc, generated from the
//   game's scripts.)
// - while the game is online, in a network game or in a network game mode, every command but wait / echo / quit
//   is refused.
// - a refusal is logged ("automation: REFUSED ...") and ends the batch.
// - keys and controllers keep working as ever: commands only add to them.
namespace frontend::automation {

// [features] automation (read once).
bool& is_switched_on();
// Once a frame from the front end's input logic (its extension): the command file, the running command, the
// status file; and the -run script.
void tick_frame(modules::FrontEnd* front_end);
// The buttons a `press` holds, added to port 0's PS2-format control data (Sio's process_sdl_device, before the bits
// are inverted: a set bit is a button down).
void add_pad_buttons(int port, std::span<uint8_t> data);

// --- the pieces (pure) ------------------------------------------------------------------------------------------

enum class Op : uint8_t {
    kRun,
    kChoose,
    kFocus,
    kPress,
    kHold,
    kStall,
    kWaitFor,
    kStageMap,
    kEcho,
    kQuit,
    kJumpTo,
    kViewCam,
    kCapture,
    kLogTricks,
    kCrash,
    kTrace,
    kFrontend
};
enum class Condition : uint8_t { kNone, kReady, kFrontEnd, kInGame, kStageMap, kMenu, kFocus, kNewFrontend };

struct Param {
    enum class Kind : uint8_t { kBitMark, kInteger, kFloat, kChars, kName } kind{};
    std::string name; // "" for a flag (its name is `text`)
    std::string text; // a string's / name's text
    int integer{};
    float real{};
};

struct OrderCode {
    Op op{};
    std::string target;         // run: the script; choose / focus: the id; press: the button; level / echo: the text;
                                // waitfor level / menu / focus: the name; capture: the folder ("" default); camera:
                                // "off" to let it go
    std::vector<Param> params;  // run
    Condition condition{};      // waitfor
    int frames{};               // press / wait / capture
    int seconds{};              // choose / focus / waitfor: the timeout
    std::vector<float> numbers; // teleport: x y z [heading]; camera: x y z dx dy dz
};

enum class CommandLineOutcome : uint8_t { kOrderCode, kVacant, kError };
// One line of the command file. Empty: a blank line or a comment.
CommandLineOutcome parse_command(const std::string& line, OrderCode* out, std::string* error);

// A pad button: its bit in the control data's button bytes (2 or 3) and its pressure byte (0: none).
struct Button {
    const char* name;
    uint8_t byte;
    uint8_t bit;
    uint8_t pressure;
};
// By name or alias (any case); null if none.
const Button* find_button(const std::string& name);
// Whether pressing it can choose something (not a direction).
bool button_chooses(const Button& button);

// An online-looking name: one of the online words between underscores, or internet / lobby / gamespy / network /
// login anywhere (any case; tools/game/make_automation_tables.ts has the same rule).
bool name_looks_online(const std::string& name);
// A script `run` refuses: online-looking, or one of the denied scripts.
bool script_denied(const std::string& name);
// A checksum naming a denied script or a denied id.
bool checksum_denied(uint32_t checksum);
// An id `choose` / `focus` refuses (its checksum; `name` too if known, "" if not).
bool id_denied(uint32_t id, const std::string& name);
// Whether a script's parameters name a denied script or id anywhere (names, structures, arrays).
bool params_denied(script::Struct* params);
// Why choosing the element is refused ("" if it isn't): a denied id, its event handlers running or naming a denied
// script, or its menus' (parents') pad_choose / pad_start handlers doing so.
std::string element_refusal(const frontend::UiWidget* element);
// Why the command is refused by its words alone (a script, a parameter, an id), or "" - before any game state.
std::string refusal_by_name(const OrderCode& command);
// The name of a checksum if known (the tables and the names commands used), else 0x<hex>.
std::string get_name_of(uint32_t checksum);
// A command as text (for the log and the status).
std::string describe(const OrderCode& command);

// The status file's contents.
struct Standing {
    uint32_t frames;
    bool ready, main_menu_, paused, loading;
    std::string where, level, game_mode_;
    std::vector<std::string> focus, selected;
    int lines_read_, lines_done_, queued;
    std::string current, last, result;
    int errors, refusals;
    std::string frontend; // the new front end's state ("" when off: not written)
};
std::string status_json(const Standing& status);

} // namespace frontend::automation
