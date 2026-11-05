#pragma once

// NOT ORIGINAL - what level_editor_triggers.cpp's pieces share (the trigger state, the records and the helpers more
// than one piece uses). They were the one file's anonymous namespace; here they are in `internal`, with a
// using-directive, so every piece sees them as it saw them there.

#include "editor/level_editor_triggers.h"
#include <cstdint>
#include <functional>
#include <new>
#include <string>
#include <vector>
#include <utility>
#include "editor/level_editor.h"
#include "engine/render/coll_movable.h"
#include "engine/script/node_manager.h"
#include "game/skate/gap_checklist.h"
#include <gsl/pointers>
#include <array>

namespace frontend::level_editor::triggers {

namespace internal {

constexpr float kHalf = 0.5F;                                    // half

constexpr float kMaxTriggerSize = 50000.0F;

bool same_text(const std::string& a, const std::string& b);

uint32_t get_checksums(const char* text);

uint32_t start_gap();

uint32_t end_gap();

uint32_t get_gap_id();

uint32_t get_score();

uint32_t get_text();

uint32_t current_node_name();

uint32_t kill_skater();

uint32_t kill_skater_water();

uint32_t get_teleport();

constexpr uint32_t kTriggerScript = 0x2CA8A299; // TriggerScript

constexpr uint16_t kVolumeFlags = 0x50;         // a trigger face the skater passes through

constexpr auto kFileName = std::to_array("editor\\thug2_decomp_triggers.qb");

constexpr float kDrawRadius = 8000.0F; // (level triggers drawn within this of the camera)

constexpr float kLabelRadius = 3000.0F;

constexpr int kMaxLevelDrawn = 300, kMaxLabels = 48, kMaxFacesKept = 64;

// NOT ORIGINAL grouping - level_editor_triggers.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_triggers_state()).
struct LevelEditorTriggersState {
    // The editor's triggers (alive or not: a deleted one's spec is kept for its undo), what Tick saw of them.
    std::vector<Spec> specs_;
    std::vector<Added> added_;
    // The level's triggers (scanned when the editor starts in a level); those switched off / on here (their walls' states
    // go in the undo steps); those switched off while an editable copy stands in for them (on again when it goes).
    std::vector<LevelTrigger> level_;
    bool scanned_{};
    std::vector<uint32_t> switched_;
    std::vector<uint32_t> turned_off_;
    uint32_t select_wall_{};
    uint32_t level_array_{};
    uint32_t inspected_{};
};

LevelEditorTriggersState& level_editor_triggers_state();

// The collision boxes.
struct Cuboid {
    uint32_t name;
    render::HitMovingBox* coll;
    V3 pos, size;
    float yaw;
};

// NOT ORIGINAL grouping - the trigger boxes, the checklist, the gaps, the undo step, the view (were globals). Made on
// first use, never destroyed (trigger_edit()).
struct TriggerEdit {
    std::vector<Cuboid> boxes_;
    // The scripts last defined, the gaps the editor put in the checklist (their names and scores) and the checklist.
    std::vector<uint8_t> defined_;
    std::vector<std::pair<std::string, int>> gaps_added_;
    goals::GapTally* checklist_{};
    std::vector<std::string> level_gap_names_;
    bool level_gaps_read_{};
    // Undo, selection, the UI's actions.
    bool has_undo_{};
    State undo_before_{};
    int select_ = -1;
    std::vector<std::function<void()>> actions_;
    bool shown_{};
    bool setting_read_{};
    V3 camera_ = {};
    V3 forward_ = {.x = 0.0F, .y = 0.0F, .z = 1.0F};
    float yaw_{};
    Calls* calls_{};
};

TriggerEdit& trigger_edit();

uint32_t hash_of_name(const std::string& name);

std::string get_label_of(uint32_t checksum);

bool node_alive(const std::string& name);

bool node_place(const std::string& name, V3* pos, float* yaw);

Calls get_walk(uint32_t script);

std::vector<std::pair<uint32_t, Calls>>& calls_cache();

const Calls& calls_of(uint32_t script);

bool is_script(uint32_t name);

void scan_level();

void free_box(render::HitMovingBox* box);

void drop_boxes();

int spec_index(const std::string& name);

bool is_sector_enabled(uint32_t checksum);

// Where a breakable goes: its position and its size.
struct BreakPlace {
    V3* pos{};
    V3* size{};
};

bool break_place(const Spec& spec, BreakPlace place);

std::vector<ProximEdit>& stored_proxims();

bool have_proxim_manager();

int proxim_index(uint32_t node);

void sync_proxims();

void get_sync();

void get_forget();

void on_array_removed(uint32_t name);

bool array_loaded(level_nodes::NodeManager* manager, uint32_t name);

void note_undo();

std::string describe(const Calls& calls, Kind kind);

} // namespace internal

// What the source file's anonymous namespace held that its pieces share: visible as it was.
using namespace internal;

} // namespace frontend::level_editor::triggers
