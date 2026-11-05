#pragma once

// NOT ORIGINAL - what automation.cpp's pieces share (the channel's constants, the runner's state, the element and
// refusal helpers more than one piece uses). They were the one file's anonymous namespace; here they are in `internal`,
// and each piece that includes this has a using-directive for them (in the piece, not here: SF.7), so it sees them as
// it saw them there.

#include "devtools/automation.h"
#include <windows.h>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>
#include "game/front_end/front_end.h"
#include "engine/ui/menu_reading.h"
#include "engine/ui/screen_element.h"
#include "game/front_end/start_level.h"
#include "engine/file/disk_files.h"
#include <gsl/pointers>

namespace frontend::automation::internal {

constexpr int kDirectionZIndex = 5;                 // a camera command's numbers: position 0-2, direction 3-5, drift 6-8

constexpr uint32_t kSkater = 0x5B8AB877;       // Skater (the local skater's alias)

constexpr int kPressWaitSeconds = 5; // press: at most this long for the menus to take pad events

constexpr int kSettleFrames = 60; // waitfor level / ingame: how long the condition must hold

uint32_t name_hash(const std::string& name);

std::string get_lower(std::string text);

bool is_identifier(const std::string& text);

bool is_hex(const std::string& text);

void remember(const std::string& name);

uint32_t id_checksum(const std::string& id);

uint32_t current_load_state();

bool in_front_end();

uint32_t ident_no(const UiWidget* element);

UiWidget* find_element(uint32_t id, ElementPtr* holder);

void focused_elements(std::vector<UiWidget*>* focused, std::vector<UiWidget*>* selected);

std::string net_refusal();

bool skater_present();

// --- trace: the skater frame by frame ---------------------------------------------------------------------------

struct Trace {
    fs::OwnedStream file;
    fs::OwnedStream objects; // (trace ... objects: the moving objects' lines)
    int left = 0;
};

Trace& trace_state();

std::string start_trace(int frames, bool objects);

void trace_frame();

// --- the command file -------------------------------------------------------------------------------------------

struct Seg {
    std::string text;
    int number;
};

struct Runner {
    // the file
    bool looked = false;
    uint64_t size = 0, time = 0, offset = 0;
    std::string partial;
    int still_frames_ = 0;
    int lines_read_ = 0, lines_done_ = 0;
    std::deque<Seg> queue;
    // the running command
    bool active = false;
    OrderCode command;
    std::string text;
    DWORD started = 0;
    int counter = 0;
    int phase = 0;
    menu_reading::Target target = {};
    bool target_set_ = false;
    // the button held (port 0)
    const Button* held = nullptr;
    // `hold`'s button (port 0), held for `hold_frames_` more frames while later commands run
    const Button* hold_button_ = nullptr;
    int hold_frames_ = 0;
    // the status
    std::string last, result = "ok";
    int errors = 0, refusals = 0;
    bool ready = false;
    std::string written;
    uint32_t written_frames_ = 0;
    // -run
    bool run_done_ = false;
    int ready_frames_ = 0;
    // the main menu: frames in a row it has been up, and whether that's a second
    int menu_frames_ = 0;
    bool menu_settled_ = false;
};

Runner& get_state();

void get_finish(Runner& runner, const std::string& result);

void get_fail(Runner& runner, const std::string& result, bool refused);

bool get_timed_out(const Runner& runner, int seconds);

std::string spawn_named(const std::string& name, const std::vector<Param>& params);

bool menus_ignore_pad(modules::FrontEnd* front_end);

std::string press_refusal(const Button& button);

bool loading_screen_up();

void step_choose(Runner& runner, modules::FrontEnd* front_end);

void write_status(Runner& runner);

bool kickoff(Runner& runner, modules::FrontEnd* front_end);

void continue_command(Runner& runner, modules::FrontEnd* front_end);

} // namespace frontend::automation::internal
