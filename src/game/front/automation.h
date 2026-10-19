#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Front {
class CScreenElement;
}
namespace Mdl {
struct FrontEnd;
}
namespace Script {
class CStruct;
}

// NOT ORIGINAL - the test-automation channel (ours, added at the user's request; not the game's and not PartyMod's):
// tests drive the menus by commands instead of posted key presses, and read where the game is from a status file.
//
// thug2-decomp.ini [features] automation=1 turns it on (default 0: off - the main install never has it on;
// tools/game/make_workspace.ps1 turns it on in the workspaces). While it's on, once a frame (the front end's input
// logic, also paused and in a level) the DLL looks at <game folder>\thug2-decomp-commands.txt (its size and time
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
//   wait <frames>
//   waitfor <condition> [seconds]         until the condition holds (default 90 s; then an error):
//                                         ready (the main menu up, taking pad events for a second: chosen away at
//                                         once, the next menu came up under a loading logo that stayed), frontend (the front end's
//                                         level loaded - SKATE NOW skates there too), ingame (a level other than the
//                                         front end, with the skater, not paused), level <name> (the skate module's
//                                         level is Load_<name>, with the skater) - those two for a second in a row,
//                                         past the level's first frames -, menu <id> (the element exists), focus <id>
//                                         (the element is focused, or a focused menu's selected item)
//   level <name>                          free skate in a level, as -level (game/front/start_level.h)
//   echo <text>                           logged and shown as the result (a marker for tools)
//   quit                                  the game closes (as its window's close button)
// A failure (a parse error, a refusal, a timeout, an element that never came) ends the batch: the lines after it
// are skipped (counted as done, logged).
//
// Status file (JSON, one object): frames, ready (the main menu has been up), main_menu (it's up now), where
// ("starting", "front_end", "level"), paused, loading (a loading screen is up - choose and press wait for it to go,
// at most 5 s), level (the skate module's level without Load_: Mainmenu, BO...; 0x<hex> if not known), game_mode,
// focus (the focused elements' ids), selected (the focused menus' selected items), lines_read, lines_done, queued,
// current (the command running), last (the last command finished), result ("ok", "error: ...", "refused: ...",
// or the echo's text), errors, refusals. Ids show as names when the name is known (the game's menu ids, game
// modes, levels, names used in commands; tools/game/make_automation_tables.py), else 0x<hex>.
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
namespace Front::Automation {

// [features] automation (read once; self-tests may set it).
bool& Enabled();
// Once a frame from the front end's input logic (MouseMenus::Update): the command file, the running command, the
// status file; and the -run script.
void Update(Mdl::FrontEnd* frontEnd);
// The buttons a `press` holds, added to port 0's PS2-format control data (Sio's ProcessSdlDevice, before the bits
// are inverted: a set bit is a button down).
void AddPadButtons(int port, uint8_t* data);

// --- the pieces (pure, self-tested) -----------------------------------------------------------------------------

enum class Op { Run, Choose, Focus, Press, Wait, WaitFor, Level, Echo, Quit };
enum class Condition { None, Ready, FrontEnd, InGame, Level, Menu, Focus };

struct Param {
    enum Kind { Flag, Integer, Float, String, Name } kind;
    std::string name;  // "" for a flag (its name is `text`)
    std::string text;  // a string's / name's text
    int integer;
    float real;
};

struct Command {
    Op op;
    std::string target;        // run: the script; choose / focus: the id; press: the button; level / echo: the text;
                               // waitfor level / menu / focus: the name
    std::vector<Param> params; // run
    Condition condition;       // waitfor
    int frames;                // press / wait
    int seconds;               // choose / focus / waitfor: the timeout
};

enum class ParseResult { Command, Empty, Error };
// One line of the command file. Empty: a blank line or a comment.
ParseResult ParseCommand(const std::string& line, Command* out, std::string* error);

// A pad button: its bit in the control data's button bytes (2 or 3) and its pressure byte (0: none).
struct Button {
    const char* name;
    uint8_t byte;
    uint8_t bit;
    uint8_t pressure;
};
// By name or alias (any case); null if none.
const Button* FindButton(const std::string& name);
// Whether pressing it can choose something (not a direction).
bool ButtonChooses(const Button& button);

// An online-looking name: one of the online words between underscores, or internet / lobby / gamespy / network /
// login anywhere (any case; tools/game/make_automation_tables.py has the same rule).
bool NameLooksOnline(const std::string& name);
// A script `run` refuses: online-looking, or one of the denied scripts.
bool ScriptDenied(const std::string& name);
// A checksum naming a denied script or a denied id.
bool ChecksumDenied(uint32_t checksum);
// An id `choose` / `focus` refuses (its checksum; `name` too if known, "" if not).
bool IdDenied(uint32_t id, const std::string& name);
// Whether a script's parameters name a denied script or id anywhere (names, structures, arrays).
bool ParamsDenied(Script::CStruct* params);
// Why choosing the element is refused ("" if it isn't): a denied id, its event handlers running or naming a denied
// script, or its menus' (parents') pad_choose / pad_start handlers doing so.
std::string ElementRefusal(const Front::CScreenElement* element);
// Why the command is refused by its words alone (a script, a parameter, an id), or "" - before any game state.
std::string RefusalByName(const Command& command);
// The name of a checksum if known (the tables and the names commands used), else 0x<hex>.
std::string NameOf(uint32_t checksum);
// A command as text (for the log and the status).
std::string Describe(const Command& command);

// The status file's contents.
struct Status {
    uint32_t frames;
    bool ready, mainMenu, paused, loading;
    std::string where, level, gameMode;
    std::vector<std::string> focus, selected;
    int linesRead, linesDone, queued;
    std::string current, last, result;
    int errors, refusals;
};
std::string StatusJson(const Status& status);

} // namespace Front::Automation
